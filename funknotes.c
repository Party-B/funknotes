/*
 * FunkNotes - Command-line note taking in C
 * Compile: gcc -o funknotes funknotes.c
 * For path do export PATH="$PATH:/path/to/funknotes" the compiled binary
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#define MAX_PATH 512
#define MAX_TEXT 1024
#define MAX_LINE 2048

typedef struct {
    char home_dir[MAX_PATH];
    char config_file[MAX_PATH];
    char projects_dir[MAX_PATH];
} Config;

// Data structures for project data
typedef struct Item {
    char timestamp[64];
    char text[MAX_TEXT];
    struct Item *next;
} Item;

typedef struct HistoryEntry {
    char action[32];
    char timestamp[64];
    char text[MAX_TEXT];
    struct HistoryEntry *next;
} HistoryEntry;

typedef struct Object {
    char name[MAX_TEXT];
    Item *items;
    HistoryEntry *history;
    struct Object *next;
} Object;

typedef struct Project {
    char name[MAX_TEXT];
    int index;
    Object *objects;
} Project;

// ===== Helper Functions ===== //
// ============================ //

/* Get current timestamp as string */
void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", t);
}

/* Read from stdin if available */
char* read_stdin() {
    if (isatty(STDIN_FILENO)) {
        return NULL;  // stdin is a terminal, not a pipe
    }

    char *buffer = malloc(MAX_TEXT);
    if (!buffer) return NULL;

    size_t len = 0;
    int c;
    while ((c = getchar()) != EOF && len < MAX_TEXT - 1) {
        buffer[len++] = c;
    }
    buffer[len] = '\0';

    // Trim trailing newline
    if (len > 0 && buffer[len-1] == '\n') {
        buffer[len-1] = '\0';
    }

    return buffer;
}

/* Initialize configuration paths */
void init_config(Config *cfg) {
    const char *home = getenv("HOME");
    snprintf(cfg->home_dir, MAX_PATH, "%s/.funknotes", home);
    snprintf(cfg->config_file, MAX_PATH, "%s/config.txt", cfg->home_dir);
    snprintf(cfg->projects_dir, MAX_PATH, "%s/projects", cfg->home_dir);
    
    mkdir(cfg->home_dir, 0755);
    mkdir(cfg->projects_dir, 0755);
}

/* Load configuration */
int load_config_data(Config *cfg, int *primary_project, int *project_counter) {
    FILE *f = fopen(cfg->config_file, "r");
    if (!f) {
        *primary_project = -1;
        *project_counter = 0;
        return 0;
    }
    
    *primary_project = -1;
    *project_counter = 0;
    
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') continue;
        
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        
        // Trim whitespace
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;
        
        if (strcmp(key, "primary_project") == 0) {
            if (strcmp(value, "-1") == 0 || strlen(value) == 0) {
                *primary_project = -1;
            } else {
                *primary_project = atoi(value);
            }
        } else if (strcmp(key, "project_counter") == 0) {
            *project_counter = atoi(value);
        }
    }
    
    fclose(f);
    return 1;
}

/* Save configuration */
void save_config_data(Config *cfg, int primary_project, int project_counter) {
    FILE *f = fopen(cfg->config_file, "w");
    if (f) {
        fprintf(f, "primary_project=%d\n", primary_project);
        fprintf(f, "project_counter=%d\n", project_counter);
        fclose(f);
    }
}

// ===== Project File I/O Functions ===== //

/* Free project memory */
void free_project(Project *proj) {
    if (!proj) return;
    Object *obj = proj->objects;
    while (obj) {
        Object *next_obj = obj->next;
        Item *item = obj->items;
        while (item) {
            Item *next_item = item->next;
            free(item);
            item = next_item;
        }
        HistoryEntry *hist = obj->history;
        while (hist) {
            HistoryEntry *next_hist = hist->next;
            free(hist);
            hist = next_hist;
        }
        free(obj);
        obj = next_obj;
    }
    free(proj);
}

/* Load project from text file */
Project* load_project_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    
    Project *proj = calloc(1, sizeof(Project));
    if (!proj) { fclose(f); return NULL; }
    
    proj->index = -1;
    Object *current_obj = NULL;
    char line[MAX_LINE];
    
    while (fgets(line, sizeof(line), f)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        // Skip empty lines
        if (line[0] == '\0') continue;
        
        // Check for object section header
        if (line[0] == '[' && strncmp(line, "[object ", 8) == 0) {
            char *obj_name_start = line + 8;
            char *obj_name_end = strchr(obj_name_start, ']');
            if (obj_name_end) {
                *obj_name_end = '\0';
                
                // Create new object
                Object *obj = calloc(1, sizeof(Object));
                strncpy(obj->name, obj_name_start, MAX_TEXT - 1);
                obj->name[MAX_TEXT - 1] = '\0';
                obj->next = proj->objects;
                proj->objects = obj;
                current_obj = obj;
            }
            continue;
        }
        
        // Parse key=value lines
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        
        // Trim whitespace
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;
        
        if (strcmp(key, "name") == 0) {
            strncpy(proj->name, value, MAX_TEXT - 1);
            proj->name[MAX_TEXT - 1] = '\0';
        } else if (strcmp(key, "index") == 0) {
            proj->index = atoi(value);
        } else if (strcmp(key, "item") == 0 && current_obj) {
            // Format: timestamp|text
            char *pipe = strchr(value, '|');
            if (pipe) {
                *pipe = '\0';
                Item *item = calloc(1, sizeof(Item));
                strncpy(item->timestamp, value, 63);
                item->timestamp[63] = '\0';
                strncpy(item->text, pipe + 1, MAX_TEXT - 1);
                item->text[MAX_TEXT - 1] = '\0';
                item->next = current_obj->items;
                current_obj->items = item;
            }
        } else if (strcmp(key, "history") == 0 && current_obj) {
            // Format: timestamp|action|text
            char *pipe1 = strchr(value, '|');
            if (pipe1) {
                *pipe1 = '\0';
                char *pipe2 = strchr(pipe1 + 1, '|');
                if (pipe2) {
                    *pipe2 = '\0';
                    HistoryEntry *hist = calloc(1, sizeof(HistoryEntry));
                    strncpy(hist->timestamp, value, 63);
                    hist->timestamp[63] = '\0';
                    strncpy(hist->action, pipe1 + 1, 31);
                    hist->action[31] = '\0';
                    strncpy(hist->text, pipe2 + 1, MAX_TEXT - 1);
                    hist->text[MAX_TEXT - 1] = '\0';
                    hist->next = current_obj->history;
                    current_obj->history = hist;
                }
            }
        }
    }
    
    fclose(f);
    return proj;
}

/* Save project to text file */
int save_project_file(const char *filename, Project *proj) {
    FILE *f = fopen(filename, "w");
    if (!f) return 0;
    
    fprintf(f, "name=%s\n", proj->name);
    fprintf(f, "index=%d\n", proj->index);
    fprintf(f, "\n");
    
    // Write objects (iterate in reverse to maintain order)
    Object *obj = proj->objects;
    if (obj) {
        // Count objects
        int obj_count = 0;
        Object *tmp = obj;
        while (tmp) { obj_count++; tmp = tmp->next; }
        
        // Create array to reverse order
        Object **objs = malloc(sizeof(Object*) * obj_count);
        tmp = obj;
        for (int i = 0; i < obj_count; i++) {
            objs[obj_count - 1 - i] = tmp;
            tmp = tmp->next;
        }
        
        // Write in original order
        for (int i = 0; i < obj_count; i++) {
            fprintf(f, "[object %s]\n", objs[i]->name);
            
            // Write items (reverse order)
            Item *item = objs[i]->items;
            if (item) {
                int item_count = 0;
                Item *tmp_item = item;
                while (tmp_item) { item_count++; tmp_item = tmp_item->next; }
                
                Item **items = malloc(sizeof(Item*) * item_count);
                tmp_item = item;
                for (int j = 0; j < item_count; j++) {
                    items[item_count - 1 - j] = tmp_item;
                    tmp_item = tmp_item->next;
                }
                
                for (int j = 0; j < item_count; j++) {
                    fprintf(f, "item=%s|%s\n", items[j]->timestamp, items[j]->text);
                }
                
                free(items);
            }
            
            // Write history (reverse order)
            HistoryEntry *hist = objs[i]->history;
            if (hist) {
                int hist_count = 0;
                HistoryEntry *tmp_hist = hist;
                while (tmp_hist) { hist_count++; tmp_hist = tmp_hist->next; }
                
                HistoryEntry **hists = malloc(sizeof(HistoryEntry*) * hist_count);
                tmp_hist = hist;
                for (int j = 0; j < hist_count; j++) {
                    hists[hist_count - 1 - j] = tmp_hist;
                    tmp_hist = tmp_hist->next;
                }
                
                for (int j = 0; j < hist_count; j++) {
                    fprintf(f, "history=%s|%s|%s\n", hists[j]->timestamp, hists[j]->action, hists[j]->text);
                }
                
                free(hists);
            }
            
            fprintf(f, "\n");
        }
        
        free(objs);
    }
    
    fclose(f);
    return 1;
}

/* Create new project */
void new_project(Config *cfg, const char *name) {
    if (strcmp(name, "projects") == 0) {
        printf("Can't create project named 'projects' (protected name)\n");
        return;
    }
    int primary, counter;
    load_config_data(cfg, &primary, &counter);
    
    counter++;
    
    char project_file[MAX_PATH];
    snprintf(project_file, MAX_PATH, "%s/%d_%s.txt", 
             cfg->projects_dir, counter, name);
    
    Project *proj = calloc(1, sizeof(Project));
    strncpy(proj->name, name, MAX_TEXT - 1);
    proj->name[MAX_TEXT - 1] = '\0';
    proj->index = counter;
    
    if (save_project_file(project_file, proj)) {
        printf("Created project '%s' with index %d\n", name, counter);
    } else {
        printf("Error creating project file\n");
    }
    
    free_project(proj);
    save_config_data(cfg, primary, counter);
}

/* Get project file by index */
int get_project_file(Config *cfg, int index, char *filename) {
    DIR *dir = opendir(cfg->projects_dir);
    if (!dir) return 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".txt")) {
            char path[MAX_PATH];
            snprintf(path, MAX_PATH, "%s/%s", cfg->projects_dir, entry->d_name);
            
            Project *proj = load_project_file(path);
            if (proj) {
                if (proj->index == index) {
                    strcpy(filename, path);
                    free_project(proj);
                    closedir(dir);
                    return 1;
                }
                free_project(proj);
            }
        }
    }
    
    closedir(dir);
    return 0;
}

/* Get project file by name or numeric identifier. If ident is numeric, treat as index.
 * On success fills `filename` with the path and sets *out_index (if non-NULL).
 * Returns 1 on found, 0 otherwise.
 */
int get_project_file_by_ident(Config *cfg, const char *ident, char *filename, int *out_index) {
    // Check if ident is a number (all digits)
    int is_number = 1;
    for (size_t i = 0; i < strlen(ident); ++i) {
        if (ident[i] < '0' || ident[i] > '9') { is_number = 0; break; }
    }

    if (is_number) {
        int idx = atoi(ident);
        if (out_index) *out_index = idx;
        return get_project_file(cfg, idx, filename);
    }

    DIR *dir = opendir(cfg->projects_dir);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".txt")) {
            char path[MAX_PATH];
            snprintf(path, MAX_PATH, "%s/%s", cfg->projects_dir, entry->d_name);

            Project *proj = load_project_file(path);
            if (proj) {
                if (strcmp(proj->name, ident) == 0) {
                    if (out_index) *out_index = proj->index;
                    strcpy(filename, path);
                    free_project(proj);
                    closedir(dir);
                    return 1;
                }
                free_project(proj);
            }
        }
    }

    closedir(dir);
    return 0;
}

/* Find object in project */
Object* find_object(Project *proj, const char *object_name) {
    Object *obj = proj->objects;
    while (obj) {
        if (strcmp(obj->name, object_name) == 0) return obj;
        obj = obj->next;
    }
    return NULL;
}

/* Count items in object */
int count_items(Object *obj) {
    int count = 0;
    Item *item = obj->items;
    while (item) {
        count++;
        item = item->next;
    }
    return count;
}

/* Get item by 1-based index (returns NULL if not found) */
Item* get_item_by_index(Object *obj, int index) {
    // Count total items first
    int total = count_items(obj);
    if (index < 1 || index > total) return NULL;
    
    // Items are stored in reverse order, so we need to traverse from end
    int target = total - index;  // Convert to 0-based from start
    Item *item = obj->items;
    for (int i = 0; i < target; i++) {
        if (!item) return NULL;
        item = item->next;
    }
    return item;
}

/* Get all items as array (caller must free) */
Item** get_items_array(Object *obj, int *count) {
    *count = count_items(obj);
    if (*count == 0) return NULL;
    
    Item **items = malloc(sizeof(Item*) * (*count));
    Item *item = obj->items;
    for (int i = *count - 1; i >= 0; i--) {
        items[i] = item;
        item = item->next;
    }
    return items;
}

/* Add object to project */
void add_object(Config *cfg, const char *object_name) {
    int primary, counter;
    load_config_data(cfg, &primary, &counter);
    
    if (primary < 0) {
        printf("No primary project set. Use 'funknotes primary <project>' first.\n");
        return;
    }
    
    char project_file[MAX_PATH];
    if (!get_project_file(cfg, primary, project_file)) {
        printf("Primary project not found\n");
        return;
    }
    
    Project *proj = load_project_file(project_file);
    if (!proj) return;
    
    if (find_object(proj, object_name)) {
        printf("Object '%s' already exists\n", object_name);
        free_project(proj);
        return;
    }
    
    Object *obj = calloc(1, sizeof(Object));
    strncpy(obj->name, object_name, MAX_TEXT - 1);
    obj->name[MAX_TEXT - 1] = '\0';
    obj->next = proj->objects;
    proj->objects = obj;
    
    if (save_project_file(project_file, proj)) {
        printf("Created object '%s' in project '%s'\n", 
               object_name, proj->name);
    }
    
    free_project(proj);
}

/* Delete an object from the primary project */
void delete_object(Config *cfg, const char *object_name) {
    int primary, counter;
    load_config_data(cfg, &primary, &counter);

    if (primary < 0) {
        printf("No primary project set. Use 'funknotes primary <project>' first.\n");
        return;
    }

    char project_file[MAX_PATH];
    if (!get_project_file(cfg, primary, project_file)) {
        printf("Primary project not found\n");
        return;
    }

    Project *proj = load_project_file(project_file);
    if (!proj) return;

    Object *obj = find_object(proj, object_name);
    if (!obj) {
        printf("Object '%s' not found\n", object_name);
        free_project(proj);
        return;
    }

    // Confirm deletion with user (default: No)
    if (isatty(STDIN_FILENO)) {
        printf("Delete object '%s'? y/N: ", object_name);
        fflush(stdout);
        char resp[8];
        if (!fgets(resp, sizeof(resp), stdin) || (resp[0] != 'y' && resp[0] != 'Y')) {
            printf("Deletion cancelled\n");
            free_project(proj);
            return;
        }
    } else {
        // Non-interactive: do not delete by default
        printf("Non-interactive mode: deletion of object '%s' aborted\n", object_name);
        free_project(proj);
        return;
    }

    // Remove the object from linked list
    if (proj->objects == obj) {
        proj->objects = obj->next;
    } else {
        Object *prev = proj->objects;
        while (prev && prev->next != obj) prev = prev->next;
        if (prev) prev->next = obj->next;
    }
    
    // Free object's items and history
    Item *item = obj->items;
    while (item) {
        Item *next = item->next;
        free(item);
        item = next;
    }
    HistoryEntry *hist = obj->history;
    while (hist) {
        HistoryEntry *next = hist->next;
        free(hist);
        hist = next;
    }
    free(obj);

    if (save_project_file(project_file, proj)) {
        printf("Deleted object '%s' from project\n", object_name);
    }

    free_project(proj);
}

/* Delete a project file by index */
void delete_project(Config *cfg, const char *ident) {
    int primary, counter;
    load_config_data(cfg, &primary, &counter);

    char project_file[MAX_PATH];
    int proj_idx = -1;
    if (!get_project_file_by_ident(cfg, ident, project_file, &proj_idx)) {
        printf("Project '%s' not found\n", ident);
        return;
    }

    // Remove the file
    if (remove(project_file) == 0) {
        printf("Delete project '%s' (index %d)? y/N: ", ident, proj_idx);
        fflush(stdout);
        char resp[8];
        if (!fgets(resp, sizeof(resp), stdin) || (resp[0] != 'y' && resp[0] != 'Y')) {
            printf("Deletion cancelled\n");
            return;
        }
        printf("Deleted project '%s' (index %d)\n", ident, proj_idx);

        // If deleted project was primary, unset primary
        if (primary == proj_idx) {
            save_config_data(cfg, -1, counter);
            printf("Primary project was deleted; primary unset.\n");
        }
    } else {
        printf("Failed to delete project file '%s'\n", project_file);
    }
}

/* Delete a specific item (1-based index) from an object in the primary project */
void delete_item_from_object(Config *cfg, const char *object_name, int item_index) {
    int primary, counter;
    load_config_data(cfg, &primary, &counter);

    if (primary < 0) {
        printf("No primary project set. Use 'funknotes primary <project>' first.\n");
        return;
    }

    char project_file[MAX_PATH];
    if (!get_project_file(cfg, primary, project_file)) {
        printf("Primary project not found\n");
        return;
    }

    Project *proj = load_project_file(project_file);
    if (!proj) return;

    Object *obj = find_object(proj, object_name);
    if (!obj) {
        printf("Object '%s' not found\n", object_name);
        free_project(proj);
        return;
    }

    int item_count = count_items(obj);
    if (item_index < 1 || item_index > item_count) {
        printf("Item %d not found in object '%s'\n", item_index, object_name);
        free_project(proj);
        return;
    }

    // Confirm deletion
    if (isatty(STDIN_FILENO)) {
        printf("Delete item %d from '%s'? y/N: ", item_index, object_name);
        fflush(stdout);
        char resp[8];
        if (!fgets(resp, sizeof(resp), stdin) || (resp[0] != 'y' && resp[0] != 'Y')) {
            printf("Deletion cancelled\n");
            free_project(proj);
            return;
        }
    } else {
        printf("Non-interactive mode: deletion of item %d aborted\n", item_index);
        free_project(proj);
        return;
    }

    // Get items as array to find the one to delete
    int count;
    Item **items = get_items_array(obj, &count);
    Item *del_item = items[item_index - 1];
    char del_text[MAX_TEXT];
    strncpy(del_text, del_item->text, MAX_TEXT - 1);
    del_text[MAX_TEXT - 1] = '\0';

    // Remove item from linked list (items are in reverse order)
    int target_idx = count - item_index;
    if (target_idx == 0) {
        // First item in list
        obj->items = del_item->next;
    } else {
        Item *prev = obj->items;
        for (int i = 0; i < target_idx - 1; i++) {
            if (!prev) break;
            prev = prev->next;
        }
        if (prev && prev->next) {
            prev->next = prev->next->next;
        }
    }
    free(del_item);
    free(items);

    // Add history entry
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    HistoryEntry *hist = calloc(1, sizeof(HistoryEntry));
    strcpy(hist->action, "DELETE_ITEM");
    strncpy(hist->timestamp, timestamp, 63);
    hist->timestamp[63] = '\0';
    strncpy(hist->text, del_text, MAX_TEXT - 1);
    hist->text[MAX_TEXT - 1] = '\0';
    hist->next = obj->history;
    obj->history = hist;

    // Write back
    if (save_project_file(project_file, proj)) {
        printf("Deleted item %d from '%s'\n", item_index, object_name);
    } else {
        printf("Failed to write project file\n");
    }

    free_project(proj);
}

/* Delete multiple items from an object. `index_list` can be comma-separated numbers and ranges like "1,3,5-7" */
void delete_items_from_object(Config *cfg, const char *object_name, const char *index_list) {
    int primary, counter;
    load_config_data(cfg, &primary, &counter);

    if (primary < 0) {
        printf("No primary project set. Use 'funknotes primary <project>' first.\n");
        return;
    }

    char project_file[MAX_PATH];
    if (!get_project_file(cfg, primary, project_file)) {
        printf("Primary project not found\n");
        return;
    }

    // Parse index_list into a set of integers
    int *indexes = NULL;
    int idx_count = 0;

    char *s = strdup(index_list);
    char *tok = strtok(s, ",");
    while (tok) {
        // Trim
        while (*tok == ' ') tok++;
        char *dash = strchr(tok, '-');
        if (dash) {
            // range a-b
            *dash = '\0';
            int a = atoi(tok);
            int b = atoi(dash + 1);
            if (a <= 0 || b <= 0) { tok = strtok(NULL, ","); continue; }
            if (a > b) { int t=a;a=b;b=t; }
            for (int v = a; v <= b; ++v) {
                indexes = realloc(indexes, sizeof(int) * (idx_count + 1));
                indexes[idx_count++] = v;
            }
        } else {
            int v = atoi(tok);
            if (v > 0) {
                indexes = realloc(indexes, sizeof(int) * (idx_count + 1));
                indexes[idx_count++] = v;
            }
        }
        tok = strtok(NULL, ",");
    }
    free(s);

    if (idx_count == 0) {
        printf("No valid indexes provided\n");
        free(indexes);
        return;
    }

    // Deduplicate and sort descending
    // Simple O(n^2) dedupe since counts are expected small
    for (int i = 0; i < idx_count; ++i) {
        for (int j = i + 1; j < idx_count; ) {
            if (indexes[i] == indexes[j]) {
                // remove j
                for (int k = j; k < idx_count - 1; ++k) indexes[k] = indexes[k+1];
                idx_count--; indexes = realloc(indexes, sizeof(int) * idx_count);
            } else j++;
        }
    }
    // sort descending
    for (int i = 0; i < idx_count; ++i) {
        for (int j = i + 1; j < idx_count; ++j) {
            if (indexes[j] > indexes[i]) { int t = indexes[i]; indexes[i] = indexes[j]; indexes[j] = t; }
        }
    }

    // Confirm deletion
    if (isatty(STDIN_FILENO)) {
        printf("Delete items %s from '%s'? y/N: ", index_list, object_name);
        fflush(stdout);
        char resp[8];
        if (!fgets(resp, sizeof(resp), stdin) || (resp[0] != 'y' && resp[0] != 'Y')) {
            printf("Deletion cancelled\n");
            free(indexes);
            return;
        }
    } else {
        printf("Non-interactive mode: deletion aborted\n");
        free(indexes);
        return;
    }

    // Load project
    Project *proj = load_project_file(project_file);
    if (!proj) { free(indexes); return; }

    Object *obj = find_object(proj, object_name);
    if (!obj) {
        printf("Object '%s' not found\n", object_name);
        free_project(proj); free(indexes); return;
    }

    int item_count = count_items(obj);
    if (item_count == 0) {
        printf("No items in object '%s'\n", object_name);
        free_project(proj); free(indexes); return;
    }

    // Build mark array
    char *mark = calloc(item_count, 1);
    int any_marked = 0;
    for (int i = 0; i < idx_count; ++i) {
        int v = indexes[i];
        if (v < 1 || v > item_count) continue;
        mark[v-1] = 1; any_marked = 1;
    }

    if (!any_marked) {
        printf("No matching items to delete\n");
        free_project(proj); free(indexes); free(mark);
        return;
    }

    // Get items as array
    int count;
    Item **items = get_items_array(obj, &count);
    
    // Delete marked items and add to history
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    
    // Delete items in reverse order (from highest index to lowest)
    for (int i = idx_count - 1; i >= 0; i--) {
        int v = indexes[i];
        if (v < 1 || v > item_count || !mark[v-1]) continue;
        
        Item *del_item = items[v - 1];
        
        // Add to history
        HistoryEntry *hist = calloc(1, sizeof(HistoryEntry));
        strcpy(hist->action, "DELETE_ITEM");
        strncpy(hist->timestamp, timestamp, 63);
        hist->timestamp[63] = '\0';
        strncpy(hist->text, del_item->text, MAX_TEXT - 1);
        hist->text[MAX_TEXT - 1] = '\0';
        hist->next = obj->history;
        obj->history = hist;
        
        // Remove from linked list (items are in reverse order)
        int target_idx = count - v;
        if (target_idx == 0) {
            obj->items = del_item->next;
        } else {
            Item *prev = obj->items;
            for (int j = 0; j < target_idx - 1; j++) {
                if (!prev) break;
                prev = prev->next;
            }
            if (prev && prev->next) {
                prev->next = prev->next->next;
            }
        }
        free(del_item);
    }
    
    free(items);
    free(mark);

    // Write back
    if (save_project_file(project_file, proj)) {
        printf("Deleted specified items from '%s'\n", object_name);
    } else {
        printf("Failed to write project file\n");
    }

    free_project(proj);
}

/* Search items across objects (or within a single object when object_name != NULL)
 * kwc = number of keywords, kws = array of keyword strings
 * Matching: case-insensitive substring match for each keyword (AND semantics)
 */
void search(Config *cfg, const char *object_name, int kwc, char **kws) {
    int primary, counter;
    load_config_data(cfg, &primary, &counter);

    if (primary < 0) {
        printf("No primary project set. Use 'funknotes primary <project>' first.\n");
        return;
    }

    char project_file[MAX_PATH];
    if (!get_project_file(cfg, primary, project_file)) {
        printf("Primary project not found\n");
        return;
    }

    Project *proj = load_project_file(project_file);
    if (!proj) return;

    if (!proj->objects) {
        printf("No objects in primary project\n");
        free_project(proj);
        return;
    }

    // For each item we will test all keywords with strcasestr (AND semantics)

    if (object_name) {
        Object *obj = find_object(proj, object_name);
        if (!obj) {
            printf("Object '%s' not found\n", object_name);
            free_project(proj);
            return;
        }

        int count;
        Item **items = get_items_array(obj, &count);

        for (int i = 0; i < count; i++) {
            const char *txt = items[i]->text;

            int ok = 1;
            for (int k = 0; k < kwc; k++) {
                if (!strcasestr(txt, kws[k])) { ok = 0; break; }
            }

            if (ok) {
                printf("%s: [%s] %s\n", object_name, items[i]->timestamp, txt);
            }
        }

        free(items);
    } else {
        // search all objects
        Object *obj = proj->objects;
        while (obj) {
            int count;
            Item **items = get_items_array(obj, &count);

            for (int i = 0; i < count; i++) {
                const char *txt = items[i]->text;

                int ok = 1;
                for (int k = 0; k < kwc; k++) {
                    if (!strcasestr(txt, kws[k])) { ok = 0; break; }
                }

                if (ok) {
                    printf("%s: [%s] %s\n", obj->name, items[i]->timestamp, txt);
                }
            }

            free(items);
            obj = obj->next;
        }
    }

    free_project(proj);
}

/* Merge multiple projects into the last project identifier (target).
 * idents: array of project identifiers (name or index), count >= 2
 */
void merge_projects(Config *cfg, int count, char **idents) {
    if (count < 2) {
        printf("Need at least two projects to merge: sources...,target\n");
        return;
    }

    // Resolve all project files
    char **paths = malloc(sizeof(char*) * count);
    int *indices = malloc(sizeof(int) * count);
    char **names = malloc(sizeof(char*) * count);
    for (int i = 0; i < count; ++i) {
        paths[i] = malloc(MAX_PATH);
        if (!get_project_file_by_ident(cfg, idents[i], paths[i], &indices[i])) {
            printf("Project '%s' not found\n", idents[i]);
            // cleanup
            for (int j = 0; j <= i; j++) free(paths[j]);
            free(paths); free(indices); free(names);
            return;
        }
        // Read project name
        Project *p = load_project_file(paths[i]);
        if (p) {
            names[i] = strdup(p->name);
            free_project(p);
        } else {
            names[i] = strdup(idents[i]);
        }
    }

    // Build prompt: sources -> target
    int target_idx = count - 1;
    printf("Merge ");
    for (int i = 0; i < target_idx; ++i) {
        printf("%s%s", names[i], i < target_idx-1 ? "," : " ");
    }
    printf("into %s? y/N: ", names[target_idx]);
    fflush(stdout);

    char resp[8];
    if (!fgets(resp, sizeof(resp), stdin) || (resp[0] != 'y' && resp[0] != 'Y')) {
        printf("Merge cancelled\n");
        for (int i=0;i<count;++i) { free(paths[i]); free(names[i]); }
        free(paths); free(indices); free(names);
        return;
    }

    // Load target
    char *target_path = paths[target_idx];
    Project *target = load_project_file(target_path);
    if (!target) { printf("Failed to load target project\n"); goto cleanup; }

    // For each source, merge
    for (int s = 0; s < target_idx; ++s) {
        char *spath = paths[s];
        Project *source = load_project_file(spath);
        if (!source) { printf("Warning: failed reading source %s\n", spath); continue; }

        if (!source->objects) { free_project(source); continue; }

        // For each object in source
        Object *sobj = source->objects;
        while (sobj) {
            Object *next_sobj = sobj->next;
            
            // Find if target already has this object
            Object *tobj = find_object(target, sobj->name);
            
            if (tobj) {
                // Append items and history
                // Get source items and history
                Item *sitem = sobj->items;
                while (sitem) {
                    Item *next_sitem = sitem->next;
                    sitem->next = tobj->items;
                    tobj->items = sitem;
                    sitem = next_sitem;
                }
                sobj->items = NULL;  // Clear so we don't free them
                
                HistoryEntry *shist = sobj->history;
                while (shist) {
                    HistoryEntry *next_shist = shist->next;
                    shist->next = tobj->history;
                    tobj->history = shist;
                    shist = next_shist;
                }
                sobj->history = NULL;  // Clear so we don't free them
                
                // Free the source object (items/history already moved)
                free(sobj);
            } else {
                // Copy object into target
                sobj->next = target->objects;
                target->objects = sobj;
            }
            
            sobj = next_sobj;
        }
        
        // Free source project (objects already moved or freed)
        source->objects = NULL;
        free_project(source);
    }

    // Write target back
    if (save_project_file(target_path, target)) {
        printf("Merged into %s\n", names[target_idx]);
        // After successful merge, prompt to delete source projects
        printf("Delete source projects? y/N: "); fflush(stdout);
        char dresp[8];
        if (fgets(dresp, sizeof(dresp), stdin) && (dresp[0] == 'y' || dresp[0] == 'Y')) {
            for (int s = 0; s < target_idx; ++s) {
                if (indices[s] > 0) {
                    char idxbuf[32]; snprintf(idxbuf, sizeof(idxbuf), "%d", indices[s]);
                    delete_project(cfg, idxbuf);
                } else {
                    delete_project(cfg, names[s]);
                }
            }
        }
    } else {
        printf("Failed to write target project\n");
    }

    free_project(target);

cleanup:
    for (int i=0;i<count;++i) { free(paths[i]); free(names[i]); }
    free(paths); free(indices); free(names);
}

/* Merge objects within a single project: provide project ident and comma-separated object list
 * Syntax: merge <project> <obj1,obj2,target>
 */
void merge_objects_in_project(Config *cfg, const char *project_ident, const char *comma_list) {
    // Resolve project
    char project_file[MAX_PATH]; int proj_idx;
    if (!get_project_file_by_ident(cfg, project_ident, project_file, &proj_idx)) {
        printf("Project '%s' not found\n", project_ident);
        return;
    }

    // Split comma_list into tokens
    char *cl = strdup(comma_list);
    int parts = 0;
    char *tok = strtok(cl, ",");
    char **objs = NULL;
    while (tok) {
        objs = realloc(objs, sizeof(char*) * (parts+1));
        objs[parts++] = strdup(tok);
        tok = strtok(NULL, ",");
    }
    free(cl);

    if (parts < 2) { printf("Need at least two objects to merge (sources,target)\n"); for (int i=0;i<parts;i++) free(objs[i]); free(objs); return; }

    // Prompt y/N
    printf("Merge ");
    for (int i=0;i<parts-1;i++) { printf("%s%s", objs[i], i < parts-2 ? "," : " "); }
    printf("into %s in project %s? y/N: ", objs[parts-1], project_ident);
    fflush(stdout);
    char resp[8];
    if (!fgets(resp, sizeof(resp), stdin) || (resp[0] != 'y' && resp[0] != 'Y')) {
        printf("Merge cancelled\n"); for (int i=0;i<parts;i++) free(objs[i]); free(objs); return;
    }

    // Load project
    Project *proj = load_project_file(project_file);
    if (!proj) { printf("Failed to load project\n"); for (int i=0;i<parts;i++) free(objs[i]); free(objs); return; }

    if (!proj->objects) { printf("No objects in project\n"); free_project(proj); for (int i=0;i<parts;i++) free(objs[i]); free(objs); return; }

    const char *target = objs[parts-1];
    Object *tobj = find_object(proj, target);
    if (!tobj) { printf("Target object '%s' not found\n", target); free_project(proj); for (int i=0;i<parts;i++) free(objs[i]); free(objs); return; }

    for (int s=0; s<parts-1; ++s) {
        Object *sobj = find_object(proj, objs[s]);
        if (!sobj) { printf("Source object '%s' not found, skipping\n", objs[s]); continue; }
        
        // Append items
        Item *sitem = sobj->items;
        while (sitem) {
            Item *next_sitem = sitem->next;
            sitem->next = tobj->items;
            tobj->items = sitem;
            sitem = next_sitem;
        }
        sobj->items = NULL;
        
        // Append history
        HistoryEntry *shist = sobj->history;
        while (shist) {
            HistoryEntry *next_shist = shist->next;
            shist->next = tobj->history;
            tobj->history = shist;
            shist = next_shist;
        }
        sobj->history = NULL;
    }

    // Write back
    if (save_project_file(project_file, proj)) {
        printf("Merged objects into %s\n", target);

        // Prompt whether to delete source objects
        printf("Delete source objects? y/N: "); fflush(stdout);
        char dresp[8];
        if (fgets(dresp, sizeof(dresp), stdin) && (dresp[0] == 'y' || dresp[0] == 'Y')) {
            for (int s = 0; s < parts-1; ++s) {
                Object *sobj = find_object(proj, objs[s]);
                if (sobj) {
                    // Remove from linked list
                    if (proj->objects == sobj) {
                        proj->objects = sobj->next;
                    } else {
                        Object *prev = proj->objects;
                        while (prev && prev->next != sobj) prev = prev->next;
                        if (prev) prev->next = sobj->next;
                    }
                    // Free object (items/history already moved)
                    free(sobj);
                }
            }
            // Write again after deletions
            if (save_project_file(project_file, proj)) {
                printf("Deleted source objects and updated project file\n");
            } else {
                printf("Failed to write project file after deletions\n");
            }
        }
    } else {
        printf("Failed to write project file\n");
    }

    free_project(proj);
    for (int i=0;i<parts;i++) free(objs[i]); free(objs);
}

/* Show items of a specific object within a specific project (by name or index) */
void show_object_in_project(Config *cfg, const char *proj_ident, const char *object_name) {
    char project_file[MAX_PATH];
    int proj_idx = -1;
    if (!get_project_file_by_ident(cfg, proj_ident, project_file, &proj_idx)) {
        printf("Project '%s' not found\n", proj_ident);
        return;
    }

    Project *proj = load_project_file(project_file);
    if (!proj) return;

    Object *obj = find_object(proj, object_name);
    if (!obj) {
        printf("Object '%s' not found in project '%s'\n", object_name, proj_ident);
        free_project(proj);
        return;
    }

    int item_count = count_items(obj);

    if (item_count == 0) {
        printf("\n=== %s/%s (empty) ===\n", proj->name, object_name);
        free_project(proj);
        return;
    }

    printf("\n=== %s/%s ===\n", proj->name, object_name);
    int count;
    Item **items = get_items_array(obj, &count);
    for (int i = 0; i < count; i++) {
        printf("%d. [%s] %s\n", i + 1, items[i]->timestamp, items[i]->text);
    }
    free(items);

    free_project(proj);
}

/* Show objects of a project or items in an object.
 * If arg is NULL -> show objects in primary project.
 * If arg matches a project (name or index) -> show that project's objects.
 * Otherwise treat arg as an object name in the primary project and show its items.
 */
void show(Config *cfg, const char *arg) {
    int primary, counter;
    load_config_data(cfg, &primary, &counter);

    char project_file[MAX_PATH];
    Project *proj = NULL;

    // If arg provided and matches a project identifier, show that project
    if (arg && get_project_file_by_ident(cfg, arg, project_file, NULL)) {
        proj = load_project_file(project_file);
        if (!proj) {
            printf("Failed to load project '%s'\n", arg);
            return;
        }

        if (!proj->objects) {
            printf("No objects in project '%s'\n", proj->name);
            free_project(proj);
            return;
        }

        printf("\n=== Objects in '%s' ===\n", proj->name);
        Object *obj = proj->objects;
        while (obj) {
            int item_count = count_items(obj);
            printf("  • %s (%d items)\n", obj->name, item_count);
            obj = obj->next;
        }

        free_project(proj);
        return;
    }

    // Otherwise, operate on primary project
    if (primary < 0) {
        printf("No primary project set. Use 'funknotes primary <project>' first.\n");
        return;
    }

    if (!get_project_file(cfg, primary, project_file)) {
        printf("Primary project not found\n");
        return;
    }

    proj = load_project_file(project_file);
    if (!proj) return;

    // If no arg provided, show all objects in primary
    if (!arg) {
        if (!proj->objects) {
            printf("No objects in project '%s'\n", proj->name);
            free_project(proj);
            return;
        }

        printf("\n=== Objects in '%s' ===\n", proj->name);
        Object *obj = proj->objects;
        while (obj) {
            int item_count = count_items(obj);
            printf("  • %s (%d items)\n", obj->name, item_count);
            obj = obj->next;
        }

        free_project(proj);
        return;
    }

    // If arg did not match a project, treat it as an object name in primary and show its items
    Object *obj = find_object(proj, arg);
    if (!obj) {
        printf("Object '%s' not found\n", arg);
        free_project(proj);
        return;
    }

    int item_count = count_items(obj);

    if (item_count == 0) {
        printf("\n=== %s (empty) ===\n", arg);
        free_project(proj);
        return;
    }

    printf("\n=== %s ===\n", arg);
    int count;
    Item **items = get_items_array(obj, &count);
    for (int i = 0; i < count; i++) {
        printf("%d. [%s] %s\n", i + 1, items[i]->timestamp, items[i]->text);
    }
    free(items);

    free_project(proj);
}

/* Add item to object */
void add_item(Config *cfg, const char *object_name, const char *text) {
    int primary, counter;
    load_config_data(cfg, &primary, &counter);
    
    if (primary < 0) {
        printf("No primary project set. Use 'funknotes primary <project>' first.\n");
        return;
    }
    
    char project_file[MAX_PATH];
    if (!get_project_file(cfg, primary, project_file)) {
        printf("Primary project not found\n");
        return;
    }
    
    Project *proj = load_project_file(project_file);
    if (!proj) return;
    
    Object *obj = find_object(proj, object_name);
    if (!obj) {
        // Object missing — prompt the user to create it (default Y)
        int create = 1; // default yes
        if (isatty(STDIN_FILENO)) {
            printf("The object '%s' does not exist, create it? Y/n: ", object_name);
            fflush(stdout);
            char resp[8];
            if (fgets(resp, sizeof(resp), stdin)) {
                if (resp[0] == 'n' || resp[0] == 'N') create = 0;
            } else {
                // EOF on stdin — keep default (create)
                create = 1;
            }
        } else {
            // Non-interactive: default to creating the object
            create = 1;
        }

        if (!create) {
            printf("Not creating object '%s'. Aborting add.\n", object_name);
            free_project(proj);
            return;
        }

        // Create the object
        free_project(proj);
        add_object(cfg, object_name);

        // Re-load project file to pick up the newly created object
        proj = load_project_file(project_file);
        if (!proj) return;
        
        obj = find_object(proj, object_name);
        if (!obj) {
            printf("Failed to create object '%s'\n", object_name);
            free_project(proj);
            return;
        }
    }
    
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    
    // Create new item
    Item *item = calloc(1, sizeof(Item));
    strncpy(item->timestamp, timestamp, 63);
    item->timestamp[63] = '\0';
    strncpy(item->text, text, MAX_TEXT - 1);
    item->text[MAX_TEXT - 1] = '\0';
    item->next = obj->items;
    obj->items = item;
    
    // Add history entry
    HistoryEntry *hist = calloc(1, sizeof(HistoryEntry));
    strcpy(hist->action, "ADD");
    strncpy(hist->timestamp, timestamp, 63);
    hist->timestamp[63] = '\0';
    strncpy(hist->text, text, MAX_TEXT - 1);
    hist->text[MAX_TEXT - 1] = '\0';
    hist->next = obj->history;
    obj->history = hist;
    
    if (save_project_file(project_file, proj)) {
        printf("Added item to %s\n", object_name);
    }
    
    free_project(proj);
}

/* Set primary project */
void set_primary(Config *cfg, const char *ident) {
    int primary, counter;
    load_config_data(cfg, &primary, &counter);

    char project_file[MAX_PATH];
    int proj_idx = -1;
    if (!get_project_file_by_ident(cfg, ident, project_file, &proj_idx)) {
        printf("Project '%s' not found\n", ident);
        return;
    }

    Project *proj = load_project_file(project_file);
    if (proj) {
        printf("Set primary project to '%s'\n", proj->name);
        free_project(proj);
    }

    save_config_data(cfg, proj_idx, counter);
}

/* List all projects */
void list_projects(Config *cfg) {
    int primary, counter;
    load_config_data(cfg, &primary, &counter);
    
    DIR *dir = opendir(cfg->projects_dir);
    if (!dir) {
        printf("No projects found\n");
        return;
    }
    
    printf("\n=== FunkNotes Projects ===\n");
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".txt")) {
            char path[MAX_PATH];
            snprintf(path, MAX_PATH, "%s/%s", cfg->projects_dir, entry->d_name);
            
            Project *proj = load_project_file(path);
            if (proj) {
                printf("  [%d] %s%s\n", proj->index, proj->name, 
                       proj->index == primary ? " (PRIMARY)" : "");
                free_project(proj);
            }
        }
    }
    
    closedir(dir);
}

/* Show usage */
void show_usage(const char *prog) {
    printf("FunkNotes - Git-like note taking\n\n");
    printf("Usage:\n");
    printf("  %s shell                          Enter interactive shell mode (type funknotes commands, exit with 'q', 'quit', 'exit', 'drop', or Ctrl+C)\n", prog);
    printf("  %s new project <name>             Create a new project\n", prog);
    printf("  %s primary <name|index>           Set primary project by name or index\n", prog);
    printf("  %s new <name>                     Create a new object\n", prog);
    printf("  %s add <object> <text>            Add item to an object\n", prog);
    printf("  %s projects                       List all projects\n", prog);
    printf("  %s show                           Show objects in primary\n", prog);
    printf("  %s show <project> <object>        Show items of an object in a specific project\n", prog);
    printf("  %s search [<object>] <keywords...>  Search notes (case-insensitive, all keywords must match)\n", prog);
    printf("  %s merge projects <proj1,proj2,...,target>   Merge multiple projects into target (last)\n", prog);
    printf("  %s merge <project> <obj1,obj2,target>  Merge objects within a project\n", prog);
    printf("  %s delete project <name|index>  Delete a project by name or index\n", prog);
    printf("  %s delete projects <proj1,proj2,...>  Delete multiple projects by name or index\n", prog);
    printf("  %s delete object <name>    Delete an object from the primary project\n", prog);
    printf("  %s delete <object> <index>  Delete a specific item (1-based) from an object in the primary project\n", prog);
}

int main(int argc, char *argv[]) {
    Config cfg;
    init_config(&cfg);
    
    if (argc < 2) {
        show_usage(argv[0]);
        return 1;
    }

    // === SHELL MODE ===
    if (strcmp(argv[1], "shell") == 0) {
        printf("FunkNotes Shell Mode. Type funknotes commands, exit with 'q', 'quit', 'exit', 'drop', or Ctrl+C.\n\n");
        char line[2048];
        while (1) {
            printf("> ");
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin)) {
                printf("\nExiting shell.\n");
                break;
            }
            // Trim leading/trailing whitespace
            char *cmd = line;
            while (*cmd == ' ' || *cmd == '\t') cmd++;
            size_t len = strlen(cmd);
            while (len > 0 && (cmd[len-1] == '\n' || cmd[len-1] == ' ' || cmd[len-1] == '\t')) cmd[--len] = 0;
            if (len == 0) continue;
            if (!strcasecmp(cmd, "clear")) {
                printf("\033[2J\033[H");
                continue;
            }
            if (!strcasecmp(cmd, "q") || !strcasecmp(cmd, "quit") || !strcasecmp(cmd, "exit") || !strcasecmp(cmd, "drop")) {
                printf("Exiting shell.\n");
                break;
            }
            // Tokenize input into argv-like array
            char *args[32]; int ac = 0;
            char *tok = strtok(cmd, " ");
            while (tok && ac < 31) { args[ac++] = tok; tok = strtok(NULL, " "); }
            args[ac] = NULL;
            if (ac == 0) continue;
            // Recursively call main() with parsed args (skip shell)
            // Build fake argv: argv[0] = "funknotes", argv[1..ac]
            char *fake_argv[33];
            fake_argv[0] = argv[0];
            for (int i = 0; i < ac; i++) fake_argv[i+1] = args[i];
            int ret = main(ac+1, fake_argv);
            if (ret != 0) printf("(error code %d)\n", ret);
        }
        return 0;
    }

    // === OPEN OBJECT SHELL MODE ===
    if (strcmp(argv[1], "open") == 0 && argc == 3) {
        // Enter object shell mode for the specified object
        int primary, counter;
        load_config_data(&cfg, &primary, &counter);
        if (primary < 0) {
            printf("No primary project set. Use 'funknotes primary <project>' first.\n");
            return 1;
        }
        char project_file[MAX_PATH];
        if (!get_project_file(&cfg, primary, project_file)) {
            printf("Primary project not found\n");
            return 1;
        }
        Project *proj = load_project_file(project_file);
        if (!proj) return 1;
        Object *obj = find_object(proj, argv[2]);
        if (!obj) {
            printf("Object '%s' not found. Creating it...\n", argv[2]);
            free_project(proj);
            add_object(&cfg, argv[2]);
            // Re-load project file
            proj = load_project_file(project_file);
            if (!proj) return 1;
            obj = find_object(proj, argv[2]);
            if (!obj) {
                free_project(proj);
                return 1;
            }
        } else {
            free_project(proj);
        }
        // Show items in object
        show(&cfg, argv[2]);
        printf("\nEnter text to add to '%s'. Type 'q', 'quit', 'exit', or Ctrl+C to leave.\n", argv[2]);
        char line[2048];
        while (1) {
            printf("%s> ", argv[2]);
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin)) {
                printf("\nExiting object shell.\n");
                break;
            }
            char *cmd = line;
            while (*cmd == ' ' || *cmd == '\t') cmd++;
            size_t len = strlen(cmd);
            while (len > 0 && (cmd[len-1] == '\n' || cmd[len-1] == ' ' || cmd[len-1] == '\t')) cmd[--len] = 0;
            if (len == 0) continue;
            if (!strcasecmp(cmd, "clear")) {
                printf("\033[2J\033[H");
                continue;
            }
            if (!strcasecmp(cmd, "q") || !strcasecmp(cmd, "quit") || !strcasecmp(cmd, "exit") || !strcasecmp(cmd, "drop")) {
                printf("Exiting object shell.\n");
                break;
            }
            if (!strcasecmp(cmd, "show")) {
                show(&cfg, argv[2]);
                continue;
            }
            if (!strcasecmp(cmd, "delete")) {
                // Enter object delete shell
                printf("Entering delete mode for '%s'. Type a number or range to delete items, or 'q', 'quit', 'exit', 'drop' to leave.\n", argv[2]);
                char dline[2048];
                while (1) {
                    printf("%s(delete)> ", argv[2]);
                    fflush(stdout);
                    if (!fgets(dline, sizeof(dline), stdin)) {
                        printf("\nExiting object delete shell.\n");
                        break;
                    }
                    char *dcmd = dline;
                    while (*dcmd == ' ' || *dcmd == '\t') dcmd++;
                    size_t dlen = strlen(dcmd);
                    while (dlen > 0 && (dcmd[dlen-1] == '\n' || dcmd[dlen-1] == ' ' || dcmd[dlen-1] == '\t')) dcmd[--dlen] = 0;
                    if (dlen == 0) continue;
                    if (!strcasecmp(dcmd, "clear")) {
                        printf("\033[2J\033[H");
                        continue;
                    }
                    if (!strcasecmp(dcmd, "q") || !strcasecmp(dcmd, "quit") || !strcasecmp(dcmd, "exit") || !strcasecmp(dcmd, "drop")) {
                        printf("Exiting object delete shell.\n");
                        break;
                    }
                    if (strchr(dcmd, ',') || strchr(dcmd, '-')) {
                        delete_items_from_object(&cfg, argv[2], dcmd);
                    } else {
                        int idx = atoi(dcmd);
                        if (idx <= 0) {
                            printf("Invalid item index '%s'\n", dcmd);
                        } else {
                            delete_item_from_object(&cfg, argv[2], idx);
                        }
                    }
                }
                continue;
            }
            add_item(&cfg, argv[2], cmd);
        }
        return 0;
    }

    // === END SHELL MODE ===
    if (strcmp(argv[1], "new") == 0) {
        if (argc == 3) {
            // Create/show object in primary project
            int primary, counter;
            load_config_data(&cfg, &primary, &counter);
            if (primary < 0) {
                printf("No primary project set. Use 'funknotes primary <project>' first.\n");
                return 1;
            }
            char project_file[MAX_PATH];
            if (!get_project_file(&cfg, primary, project_file)) {
                printf("Primary project not found\n");
                return 1;
            }
            Project *proj = load_project_file(project_file);
            if (!proj) return 1;
            Object *obj = find_object(proj, argv[2]);
            if (obj) {
                // Object exists, enter shell mode to add items
                int item_count = count_items(obj);
                printf("\n=== %s ===\n", argv[2]);
                int count;
                Item **items = get_items_array(obj, &count);
                for (int i = 0; i < count; i++) {
                    printf("%d. [%s] %s\n", i + 1, items[i]->timestamp, items[i]->text);
                }
                free(items);
                free_project(proj);
                printf("\nEnter text to add to '%s'. Type 'q', 'quit', 'exit', or Ctrl+C to leave.\n", argv[2]);
                char line[2048];
                while (1) {
                    printf("%s> ", argv[2]);
                    fflush(stdout);
                    if (!fgets(line, sizeof(line), stdin)) {
                        printf("\nExiting object shell.\n");
                        break;
                    }
                    // Trim leading/trailing whitespace
                    char *cmd = line;
                    while (*cmd == ' ' || *cmd == '\t') cmd++;
                    size_t len = strlen(cmd);
                    while (len > 0 && (cmd[len-1] == '\n' || cmd[len-1] == ' ' || cmd[len-1] == '\t')) cmd[--len] = 0;
                    if (len == 0) continue;
                    if (!strcasecmp(cmd, "clear")) {
                        printf("\033[2J\033[H");
                        continue;
                    }
                    if (!strcasecmp(cmd, "q") || !strcasecmp(cmd, "quit") || !strcasecmp(cmd, "exit") || !strcasecmp(cmd, "drop")) {
                        printf("Exiting object shell.\n");
                        break;
                    }
                    // Add item to object
                    add_item(&cfg, argv[2], cmd);
                }
                return 0;
            } else {
                // Object does not exist, create it
                free_project(proj);
                add_object(&cfg, argv[2]);
                return 0;
            }
        } else if (argc == 4 && strcmp(argv[2], "project") == 0) {
            new_project(&cfg, argv[3]);
            return 0;
        } else {
            printf("Usage: funknotes new <object> OR funknotes new project <name>\n");
            return 1;
        }
    }
    else if (strcmp(argv[1], "primary") == 0 && argc == 3) {
        set_primary(&cfg, argv[2]);
    }
    else if (strcmp(argv[1], "show") == 0) {
       if (argc == 2) {
            show(&cfg, NULL);  // Show all objects in primary
        } else if (argc == 3) {
            show(&cfg, argv[2]);  // Show objects in project or object in primary
        } else if (argc == 4) {
            // show <project> <object>
            show_object_in_project(&cfg, argv[2], argv[3]);
        } else {
            show_usage(argv[0]);
        }
    }
    else if (strcmp(argv[1], "add") == 0 && argc >= 3) {
        char *text = read_stdin();
        if (text) {
            // Text from stdin
            add_item(&cfg, argv[2], text);
            free(text);
        }
        else if (argc >= 4) {
            // Text from arguments
            char text_buf[MAX_TEXT] = "";
            for (int i = 3; i < argc; i++) {
                strcat(text_buf, argv[i]);
                if (i < argc - 1) strcat(text_buf, " ");
            }
            add_item(&cfg, argv[2], text_buf);
        }
        else {
            // Enter object shell mode for adding items interactively
            int primary, counter;
            load_config_data(&cfg, &primary, &counter);
            if (primary < 0) {
                printf("No primary project set. Use 'funknotes primary <project>' first.\n");
                return 1;
            }
            char project_file[MAX_PATH];
            if (!get_project_file(&cfg, primary, project_file)) {
                printf("Primary project not found\n");
                return 1;
            }
            Project *proj = load_project_file(project_file);
            if (!proj) return 1;
            Object *obj = find_object(proj, argv[2]);
            if (!obj) {
                printf("Object '%s' not found. Creating it...\n", argv[2]);
                free_project(proj);
                add_object(&cfg, argv[2]);
                // Re-load project file
                proj = load_project_file(project_file);
                if (!proj) return 1;
                obj = find_object(proj, argv[2]);
                if (!obj) {
                    free_project(proj);
                    return 1;
                }
                free_project(proj);
            } else {
                free_project(proj);
            }
            // Show items in object
            show(&cfg, argv[2]);
            printf("\nEnter text to add to '%s'. Type 'q', 'quit', 'exit', or Ctrl+C to leave.\n", argv[2]);
            char line[2048];
            while (1) {
                printf("%s> ", argv[2]);
                fflush(stdout);
                if (!fgets(line, sizeof(line), stdin)) {
                    printf("\nExiting object shell.\n");
                    break;
                }
                char *cmd = line;
                while (*cmd == ' ' || *cmd == '\t') cmd++;
                size_t len = strlen(cmd);
                while (len > 0 && (cmd[len-1] == '\n' || cmd[len-1] == ' ' || cmd[len-1] == '\t')) cmd[--len] = 0;
                if (len == 0) continue;
                if (!strcasecmp(cmd, "clear")) {
                    printf("\033[2J\033[H");
                    continue;
                }
                if (!strcasecmp(cmd, "q") || !strcasecmp(cmd, "quit") || !strcasecmp(cmd, "exit") || !strcasecmp(cmd, "drop")) {
                    printf("Exiting object shell.\n");
                    break;
                }
                add_item(&cfg, argv[2], cmd);
            }
        }
    }
    else if (strcmp(argv[1], "search") == 0 && argc >= 3) {
        // Determine whether first token is an object name in the primary project
        int primary, counter;
        load_config_data(&cfg, &primary, &counter);
        if (primary < 0) {
            printf("No primary project set. Use 'funknotes primary <project>' first.\n");
        } else {
            char project_file[MAX_PATH];
            if (!get_project_file(&cfg, primary, project_file)) {
                printf("Primary project not found\n");
            } else {
                Project *proj = load_project_file(project_file);
                if (!proj) { show_usage(argv[0]); }
                else {
                    const char *maybe_obj = argv[2];
                    const char *obj_name = NULL;
                    int kw_start = 2;

                    if (proj) {
                        Object *tmp = find_object(proj, maybe_obj);
                        if (tmp) {
                            // first token is an object name
                            obj_name = maybe_obj;
                            kw_start = 3;
                        }
                        free_project(proj);
                    }

                    if (kw_start > argc - 1) {
                        // no keywords provided
                        show_usage(argv[0]);
                    } else {
                        int kwc = argc - kw_start;
                        char **kws = &argv[kw_start];
                        search(&cfg, obj_name, kwc, kws);
                    }
                }
            }
        }
    }
    else if (strcmp(argv[1], "merge") == 0) {
        // Two modes:
        // 1) funknotes merge proj1,proj2,proj3  (argc==3)
        // 2) funknotes merge <project> <obj1,obj2,target> (argc==4)
        if (argc == 4 && strcmp(argv[2], "projects") == 0) {
            // project-merge mode: split comma-separated idents in argv[3]
            char *s = strdup(argv[3]);
            int parts = 0;
            char *tok = strtok(s, ",");
            char **idents = NULL;
            while (tok) {
                idents = realloc(idents, sizeof(char*) * (parts+1));
                idents[parts++] = strdup(tok);
                tok = strtok(NULL, ",");
            }
            free(s);
            if (parts >= 2) {
                merge_projects(&cfg, parts, idents);
            } else {
                show_usage(argv[0]);
            }
            for (int i=0;i<parts;i++) free(idents[i]); free(idents);
        } else if (argc == 4) {
            merge_objects_in_project(&cfg, argv[2], argv[3]);
        } else {
            show_usage(argv[0]);
        }
    }
    else if (strcmp(argv[1], "projects") == 0) {
        list_projects(&cfg);
    }
    else if (strcmp(argv[1], "delete") == 0) {
        // Enhanced: if argc == 3 and argv[2] is not a keyword, prompt for delete mode
        if (argc == 4) {
            if (strcmp(argv[2], "project") == 0) {
                delete_project(&cfg, argv[3]);
            } else if (strcmp(argv[2], "projects") == 0) {
                char *s = strdup(argv[3]);
                if (!s) { show_usage(argv[0]); }
                else {
                    char *tok = strtok(s, ",");
                    while (tok) {
                        delete_project(&cfg, tok);
                        tok = strtok(NULL, ",");
                    }
                    free(s);
                }
            } else if (strcmp(argv[2], "object") == 0) {
                delete_object(&cfg, argv[3]);
            } else {
                if (strchr(argv[3], ',') || strchr(argv[3], '-')) {
                    delete_items_from_object(&cfg, argv[2], argv[3]);
                } else {
                    int idx = atoi(argv[3]);
                    if (idx <= 0) {
                        printf("Invalid item index '%s'\n", argv[3]);
                    } else {
                        delete_item_from_object(&cfg, argv[2], idx);
                    }
                }
            }
        } else if (argc == 3) {
            // Prompt: 1. delete entire object, 2. delete item in object
            printf("Delete '%s':\n  1. delete entire object\n  2. delete item in object\nSelect (1/2): ", argv[2]);
            fflush(stdout);
            char resp[8];
            if (!fgets(resp, sizeof(resp), stdin)) {
                printf("Aborted.\n");
                return 1;
            }
            int sel = atoi(resp);
            if (sel == 1) {
                delete_object(&cfg, argv[2]);
            } else if (sel == 2) {
                // Enter object shell for deletion
                int primary, counter;
                load_config_data(&cfg, &primary, &counter);
                if (primary < 0) {
                    printf("No primary project set. Use 'funknotes primary <project>' first.\n");
                    return 1;
                }
                char project_file[MAX_PATH];
                if (!get_project_file(&cfg, primary, project_file)) {
                    printf("Primary project not found\n");
                    return 1;
                }
                Project *proj = load_project_file(project_file);
                if (!proj) return 1;
                Object *obj = find_object(proj, argv[2]);
                if (!obj) {
                    printf("Object '%s' not found\n", argv[2]);
                    free_project(proj);
                    return 1;
                }
                int item_count = count_items(obj);
                printf("\n=== %s ===\n", argv[2]);
                int count;
                Item **items = get_items_array(obj, &count);
                for (int i = 0; i < count; i++) {
                    printf("%d. [%s] %s\n", i + 1, items[i]->timestamp, items[i]->text);
                }
                free(items);
                free_project(proj);
                printf("\nType 'delete <index>' or 'delete <range>' (e.g. 'delete 2', 'delete 2-5'), or 'q', 'quit', 'exit', 'drop' to leave.\n");
                char line[2048];
                while (1) {
                    printf("%s(delete)> ", argv[2]);
                    fflush(stdout);
                    if (!fgets(line, sizeof(line), stdin)) {
                        printf("\nExiting object delete shell.\n");
                        break;
                    }
                    char *cmd = line;
                    while (*cmd == ' ' || *cmd == '\t') cmd++;
                    size_t len = strlen(cmd);
                    while (len > 0 && (cmd[len-1] == '\n' || cmd[len-1] == ' ' || cmd[len-1] == '\t')) cmd[--len] = 0;
                    if (len == 0) continue;
                    if (!strcasecmp(cmd, "clear")) {
                        printf("\033[2J\033[H");
                        continue;
                    }
                    if (!strcasecmp(cmd, "q") || !strcasecmp(cmd, "quit") || !strcasecmp(cmd, "exit") || !strcasecmp(cmd, "drop")) {
                        printf("Exiting object delete shell.\n");
                        break;
                    }
                    // Accept: delete <index>, delete <range>, <index>, <range>
                    char *tok = strtok(cmd, " ");
                    if (tok && strcasecmp(tok, "delete") == 0) {
                        char *arg = strtok(NULL, " ");
                        if (!arg) {
                            printf("Usage: delete <index> or delete <range>\n");
                            continue;
                        }
                        if (strchr(arg, ',') || strchr(arg, '-')) {
                            delete_items_from_object(&cfg, argv[2], arg);
                        } else {
                            int idx = atoi(arg);
                            if (idx <= 0) {
                                printf("Invalid item index '%s'\n", arg);
                            } else {
                                delete_item_from_object(&cfg, argv[2], idx);
                            }
                        }
                    } else if (tok) {
                        // If input is just a number or range
                        if (strchr(tok, ',') || strchr(tok, '-')) {
                            delete_items_from_object(&cfg, argv[2], tok);
                        } else {
                            int idx = atoi(tok);
                            if (idx <= 0) {
                                printf("Invalid item index '%s'\n", tok);
                            } else {
                                delete_item_from_object(&cfg, argv[2], idx);
                            }
                        }
                    }
                }
            } else {
                printf("Aborted.\n");
            }
        } else {
            show_usage(argv[0]);
        }
    }
    else if (strcmp(argv[1], "help") == 0) {
        show_usage(argv[0]);
    }
    else {
        show_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
