/*
 * FunkNotes - Command-line note taking in C
 * Compile: gcc -o funknotes funknotes.c -ljson-c
 * Requires: libjson-c-dev
 * For path do export PATH="$PATH:/path/to/funknotes" the compiled binary
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <json-c/json.h>
#include <unistd.h>
#include <dirent.h>

#define MAX_PATH 512
#define MAX_TEXT 1024

typedef struct {
    char home_dir[MAX_PATH];
    char config_file[MAX_PATH];
    char projects_dir[MAX_PATH];
} Config;

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
    snprintf(cfg->config_file, MAX_PATH, "%s/config.json", cfg->home_dir);
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
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);
    
    struct json_object *root = json_tokener_parse(content);
    free(content);
    
    if (!root) {
        *primary_project = -1;
        *project_counter = 0;
        return 0;
    }
    
    struct json_object *pp, *pc;
    json_object_object_get_ex(root, "primary_project", &pp);
    json_object_object_get_ex(root, "project_counter", &pc);
    
    *primary_project = pp && !json_object_is_type(pp, json_type_null) ? 
                       json_object_get_int(pp) : -1;
    *project_counter = pc ? json_object_get_int(pc) : 0;
    
    json_object_put(root);
    return 1;
}

/* Save configuration */
void save_config_data(Config *cfg, int primary_project, int project_counter) {
    struct json_object *root = json_object_new_object();
    
    if (primary_project >= 0) {
        json_object_object_add(root, "primary_project", 
                             json_object_new_int(primary_project));
    } else {
        json_object_object_add(root, "primary_project", NULL);
    }
    json_object_object_add(root, "project_counter", 
                         json_object_new_int(project_counter));
    
    FILE *f = fopen(cfg->config_file, "w");
    if (f) {
        fprintf(f, "%s\n", json_object_to_json_string_ext(root, 
                JSON_C_TO_STRING_PRETTY));
        fclose(f);
    }
    
    json_object_put(root);
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
    snprintf(project_file, MAX_PATH, "%s/%d_%s.json", 
             cfg->projects_dir, counter, name);
    
    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "name", json_object_new_string(name));
    json_object_object_add(root, "index", json_object_new_int(counter));
    json_object_object_add(root, "objects", json_object_new_object());
    json_object_object_add(root, "commits", json_object_new_array());
    
    FILE *f = fopen(project_file, "w");
    if (f) {
        fprintf(f, "%s\n", json_object_to_json_string_ext(root, 
                JSON_C_TO_STRING_PRETTY));
        fclose(f);
        printf("Created project '%s' with index %d\n", name, counter);
    } else {
        printf("Error creating project file\n");
    }
    
    json_object_put(root);
    save_config_data(cfg, primary, counter);
}

/* Get project file by index */
int get_project_file(Config *cfg, int index, char *filename) {
    DIR *dir = opendir(cfg->projects_dir);
    if (!dir) return 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".json")) {
            char path[MAX_PATH];
            snprintf(path, MAX_PATH, "%s/%s", cfg->projects_dir, entry->d_name);
            
            FILE *f = fopen(path, "r");
            if (!f) continue;
            
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            char *content = malloc(size + 1);
            fread(content, 1, size, f);
            content[size] = '\0';
            fclose(f);
            
            struct json_object *root = json_tokener_parse(content);
            free(content);
            
            if (root) {
                struct json_object *idx;
                json_object_object_get_ex(root, "index", &idx);
                int proj_idx = json_object_get_int(idx);
                json_object_put(root);
                
                if (proj_idx == index) {
                    strcpy(filename, path);
                    closedir(dir);
                    return 1;
                }
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
        if (strstr(entry->d_name, ".json")) {
            char path[MAX_PATH];
            snprintf(path, MAX_PATH, "%s/%s", cfg->projects_dir, entry->d_name);

            FILE *f = fopen(path, "r");
            if (!f) continue;

            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);

            char *content = malloc(size + 1);
            fread(content, 1, size, f);
            content[size] = '\0';
            fclose(f);

            struct json_object *root = json_tokener_parse(content);
            free(content);

            if (root) {
                struct json_object *name, *idx;
                json_object_object_get_ex(root, "name", &name);
                json_object_object_get_ex(root, "index", &idx);
                if (name && json_object_get_string(name) && strcmp(json_object_get_string(name), ident) == 0) {
                    if (out_index) *out_index = json_object_get_int(idx);
                    strcpy(filename, path);
                    json_object_put(root);
                    closedir(dir);
                    return 1;
                }
                json_object_put(root);
            }
        }
    }

    closedir(dir);
    return 0;
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
    
    FILE *f = fopen(project_file, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);
    
    struct json_object *root = json_tokener_parse(content);
    free(content);
    
    if (!root) return;
    
    struct json_object *objects, *proj_name;
    json_object_object_get_ex(root, "objects", &objects);
    json_object_object_get_ex(root, "name", &proj_name);
    
    if (json_object_object_get_ex(objects, object_name, NULL)) {
        printf("Object '%s' already exists\n", object_name);
        json_object_put(root);
        return;
    }
    
    struct json_object *obj_data = json_object_new_object();
    json_object_object_add(obj_data, "items", json_object_new_array());
    json_object_object_add(obj_data, "history", json_object_new_array());
    json_object_object_add(objects, object_name, obj_data);
    
    f = fopen(project_file, "w");
    if (f) {
        fprintf(f, "%s\n", json_object_to_json_string_ext(root, 
                JSON_C_TO_STRING_PRETTY));
        fclose(f);
        printf("Created object '%s' in project '%s'\n", 
               object_name, json_object_get_string(proj_name));
    }
    
    json_object_put(root);
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

    FILE *f = fopen(project_file, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    struct json_object *root = json_tokener_parse(content);
    free(content);

    if (!root) return;

    struct json_object *objects;
    json_object_object_get_ex(root, "objects", &objects);

    if (!json_object_object_get_ex(objects, object_name, NULL)) {
        printf("Object '%s' not found\n", object_name);
        json_object_put(root);
        return;
    }

    // Confirm deletion with user (default: No)
    if (isatty(STDIN_FILENO)) {
        printf("Delete object '%s'? y/N: ", object_name);
        fflush(stdout);
        char resp[8];
        if (!fgets(resp, sizeof(resp), stdin) || (resp[0] != 'y' && resp[0] != 'Y')) {
            printf("Deletion cancelled\n");
            json_object_put(root);
            return;
        }
    } else {
        // Non-interactive: do not delete by default
        printf("Non-interactive mode: deletion of object '%s' aborted\n", object_name);
        json_object_put(root);
        return;
    }

    // Remove the object
    json_object_object_del(objects, object_name);

    f = fopen(project_file, "w");
    if (f) {
        fprintf(f, "%s\n", json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
        fclose(f);
        printf("Deleted object '%s' from project\n", object_name);
    }

    json_object_put(root);
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

    FILE *f = fopen(project_file, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    struct json_object *root = json_tokener_parse(content);
    free(content);

    if (!root) return;

    struct json_object *objects;
    json_object_object_get_ex(root, "objects", &objects);
    if (!objects) {
        printf("No objects in primary project\n");
        json_object_put(root);
        return;
    }

    struct json_object *obj;
    if (!json_object_object_get_ex(objects, object_name, &obj)) {
        printf("Object '%s' not found\n", object_name);
        json_object_put(root);
        return;
    }

    struct json_object *items;
    json_object_object_get_ex(obj, "items", &items);
    int item_count = json_object_array_length(items);

    if (item_index < 1 || item_index > item_count) {
        printf("Item %d not found in object '%s'\n", item_index, object_name);
        json_object_put(root);
        return;
    }

    // Confirm deletion
    if (isatty(STDIN_FILENO)) {
        printf("Delete item %d from '%s'? y/N: ", item_index, object_name);
        fflush(stdout);
        char resp[8];
        if (!fgets(resp, sizeof(resp), stdin) || (resp[0] != 'y' && resp[0] != 'Y')) {
            printf("Deletion cancelled\n");
            json_object_put(root);
            return;
        }
    } else {
        printf("Non-interactive mode: deletion of item %d aborted\n", item_index);
        json_object_put(root);
        return;
    }

    // Capture deleted item's text for history
    struct json_object *del_item = json_object_array_get_idx(items, item_index - 1);
    struct json_object *dtext;
    json_object_object_get_ex(del_item, "text", &dtext);
    const char *del_text = dtext ? json_object_get_string(dtext) : "";

    // Build new items array without the deleted index
    struct json_object *new_items = json_object_new_array();
    for (int i = 0; i < item_count; ++i) {
        if (i == item_index - 1) continue;
        struct json_object *it = json_object_array_get_idx(items, i);
        json_object_array_add(new_items, json_object_get(it));
    }

    // Replace items
    json_object_object_add(obj, "items", new_items);

    // Append history entry
    char timestamp[64]; get_timestamp(timestamp, sizeof(timestamp));
    struct json_object *hist_entry = json_object_new_object();
    json_object_object_add(hist_entry, "action", json_object_new_string("DELETE_ITEM"));
    json_object_object_add(hist_entry, "timestamp", json_object_new_string(timestamp));
    json_object_object_add(hist_entry, "text", json_object_new_string(del_text));

    struct json_object *history;
    json_object_object_get_ex(obj, "history", &history);
    if (!history) {
        history = json_object_new_array();
        json_object_object_add(obj, "history", history);
    }
    json_object_array_add(history, hist_entry);

    // Write back
    FILE *fw = fopen(project_file, "w");
    if (fw) {
        fprintf(fw, "%s\n", json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
        fclose(fw);
        printf("Deleted item %d from '%s'\n", item_index, object_name);
    } else {
        printf("Failed to write project file\n");
    }

    json_object_put(root);
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

    // Load project JSON
    FILE *f = fopen(project_file, "r");
    if (!f) { free(indexes); return; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    char *content = malloc(size + 1); fread(content, 1, size, f); content[size] = '\0'; fclose(f);
    struct json_object *root = json_tokener_parse(content); free(content);
    if (!root) { free(indexes); return; }

    struct json_object *objects;
    json_object_object_get_ex(root, "objects", &objects);
    if (!objects) { printf("No objects in project\n"); json_object_put(root); free(indexes); return; }

    struct json_object *obj;
    if (!json_object_object_get_ex(objects, object_name, &obj)) {
        printf("Object '%s' not found\n", object_name);
        json_object_put(root); free(indexes); return;
    }

    struct json_object *items;
    json_object_object_get_ex(obj, "items", &items);
    int item_count = json_object_array_length(items);

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
        json_object_put(root); free(indexes); free(mark);
        return;
    }

    // Create new items array and collect deleted texts for history
    struct json_object *new_items = json_object_new_array();
    // ensure history exists
    struct json_object *history; json_object_object_get_ex(obj, "history", &history);
    if (!history) { history = json_object_new_array(); json_object_object_add(obj, "history", history); }

    for (int i = 0; i < item_count; ++i) {
        struct json_object *it = json_object_array_get_idx(items, i);
        if (mark[i]) {
            struct json_object *dtext; json_object_object_get_ex(it, "text", &dtext);
            const char *txt = dtext ? json_object_get_string(dtext) : "";
            char timestamp[64]; get_timestamp(timestamp, sizeof(timestamp));
            struct json_object *he = json_object_new_object();
            json_object_object_add(he, "action", json_object_new_string("DELETE_ITEM"));
            json_object_object_add(he, "timestamp", json_object_new_string(timestamp));
            json_object_object_add(he, "text", json_object_new_string(txt));
            json_object_array_add(history, he);
            // skip adding to new_items
        } else {
            json_object_array_add(new_items, json_object_get(it));
        }
    }

    // Replace items
    json_object_object_add(obj, "items", new_items);

    // Write back
    FILE *fw = fopen(project_file, "w");
    if (fw) {
        fprintf(fw, "%s\n", json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
        fclose(fw);
        printf("Deleted specified items from '%s'\n", object_name);
    } else {
        printf("Failed to write project file\n");
    }

    json_object_put(root);
    free(indexes); free(mark);
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

    FILE *f = fopen(project_file, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    struct json_object *root = json_tokener_parse(content);
    free(content);

    if (!root) return;

    struct json_object *objects;
    json_object_object_get_ex(root, "objects", &objects);

    if (!objects) {
        printf("No objects in primary project\n");
        json_object_put(root);
        return;
    }

    // For each item we will test all keywords with strcasestr (AND semantics)

    if (object_name) {
        struct json_object *obj;
        if (!json_object_object_get_ex(objects, object_name, &obj)) {
            printf("Object '%s' not found\n", object_name);
            json_object_put(root);
            return;
        }

        struct json_object *items;
        json_object_object_get_ex(obj, "items", &items);
        int item_count = json_object_array_length(items);

        for (int i = 0; i < item_count; i++) {
            struct json_object *item = json_object_array_get_idx(items, i);
            struct json_object *timestamp, *text;
            json_object_object_get_ex(item, "timestamp", &timestamp);
            json_object_object_get_ex(item, "text", &text);

            const char *txt = json_object_get_string(text);

            int ok = 1;
            for (int k = 0; k < kwc; k++) {
                if (!strcasestr(txt, kws[k])) { ok = 0; break; }
            }

            if (ok) {
                printf("%s: [%s] %s\n", object_name,
                       json_object_get_string(timestamp), txt);
            }
        }
    } else {
        // search all objects
        json_object_object_foreach(objects, key, val) {
            struct json_object *items;
            json_object_object_get_ex(val, "items", &items);
            int item_count = json_object_array_length(items);

            for (int i = 0; i < item_count; i++) {
                struct json_object *item = json_object_array_get_idx(items, i);
                struct json_object *timestamp, *text;
                json_object_object_get_ex(item, "timestamp", &timestamp);
                json_object_object_get_ex(item, "text", &text);

                const char *txt = json_object_get_string(text);

                int ok = 1;
                for (int k = 0; k < kwc; k++) {
                    if (!strcasestr(txt, kws[k])) { ok = 0; break; }
                }

                if (ok) {
                    printf("%s: [%s] %s\n", key,
                           json_object_get_string(timestamp), txt);
                }
            }
        }
    }

    json_object_put(root);
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
        FILE *f = fopen(paths[i], "r");
        if (!f) { printf("Failed reading %s\n", paths[i]); for (int j=0;j<count;++j) free(paths[j]); free(paths); free(indices); free(names); return; }
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        char *buf = malloc(sz+1); fread(buf,1,sz,f); buf[sz]='\0'; fclose(f);
        struct json_object *r = json_tokener_parse(buf);
        free(buf);
        if (r) {
            struct json_object *n; json_object_object_get_ex(r, "name", &n);
            const char *nm = n ? json_object_get_string(n) : NULL;
            names[i] = nm ? strdup(nm) : strdup(idents[i]);
            json_object_put(r);
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
    FILE *ft = fopen(target_path, "r");
    if (!ft) { printf("Failed to open target project\n"); goto cleanup; }
    fseek(ft,0,SEEK_END); long tsz = ftell(ft); fseek(ft,0,SEEK_SET);
    char *tbuf = malloc(tsz+1); fread(tbuf,1,tsz,ft); tbuf[tsz]='\0'; fclose(ft);
    struct json_object *troot = json_tokener_parse(tbuf); free(tbuf);
    if (!troot) { printf("Failed to parse target project json\n"); goto cleanup; }

    struct json_object *tobjects; json_object_object_get_ex(troot, "objects", &tobjects);
    if (!tobjects) {
        tobjects = json_object_new_object();
        json_object_object_add(troot, "objects", tobjects);
    }

    // For each source, merge
    for (int s = 0; s < target_idx; ++s) {
        char *spath = paths[s];
        FILE *fs = fopen(spath, "r");
        if (!fs) { printf("Warning: failed reading source %s\n", spath); continue; }
        fseek(fs,0,SEEK_END); long ssz = ftell(fs); fseek(fs,0,SEEK_SET);
        char *sbuf = malloc(ssz+1); fread(sbuf,1,ssz,fs); sbuf[ssz]='\0'; fclose(fs);
        struct json_object *sroot = json_tokener_parse(sbuf); free(sbuf);
        if (!sroot) { printf("Warning: parse failed for %s\n", spath); continue; }

        struct json_object *sobjects; json_object_object_get_ex(sroot, "objects", &sobjects);
        if (!sobjects) { json_object_put(sroot); continue; }

        json_object_object_foreach(sobjects, key, val) {
            // If target has the object, append items and history; else copy object
            struct json_object *tobj;
            if (json_object_object_get_ex(tobjects, key, &tobj)) {
                struct json_object *titems, *thistory, *sitems, *shistory;
                json_object_object_get_ex(tobj, "items", &titems);
                json_object_object_get_ex(tobj, "history", &thistory);
                json_object_object_get_ex(val, "items", &sitems);
                json_object_object_get_ex(val, "history", &shistory);
                if (sitems && titems) {
                    int len = json_object_array_length(sitems);
                    for (int i=0;i<len;i++) {
                        struct json_object *it = json_object_array_get_idx(sitems, i);
                        json_object_array_add(titems, json_object_get(it));
                    }
                }
                if (shistory && thistory) {
                    int len = json_object_array_length(shistory);
                    for (int i=0;i<len;i++) {
                        struct json_object *it = json_object_array_get_idx(shistory, i);
                        json_object_array_add(thistory, json_object_get(it));
                    }
                }
            } else {
                // copy object into target (increment refcount)
                json_object_object_add(tobjects, key, json_object_get(val));
            }
        }

        json_object_put(sroot);
    }

    // Write target back
    FILE *fw = fopen(target_path, "w");
    if (fw) {
        fprintf(fw, "%s\n", json_object_to_json_string_ext(troot, JSON_C_TO_STRING_PRETTY));
        fclose(fw);
        printf("Merged into %s\n", names[target_idx]);
        // After successful merge, prompt to delete source projects
        printf("Delete source projects? y/N: "); fflush(stdout);
        char dresp[8];
        if (fgets(dresp, sizeof(dresp), stdin) && (dresp[0] == 'y' || dresp[0] == 'Y')) {
            for (int s = 0; s < target_idx; ++s) {
                // Use the original identifier strings passed to the caller (idents)
                // We don't have idents here; caller typically passed strdup'd idents and will free them.
                // As a best-effort, delete by name using names[s] (project display name) and indices if available.
                // Prefer deletion by index if we resolved it earlier
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

    json_object_put(troot);

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
    FILE *f = fopen(project_file, "r"); if (!f) { printf("Failed to open project\n"); for (int i=0;i<parts;i++) free(objs[i]); free(objs); return; }
    fseek(f,0,SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET);
    char *buf = malloc(sz+1); fread(buf,1,sz,f); buf[sz]='\0'; fclose(f);
    struct json_object *root = json_tokener_parse(buf); free(buf);
    if (!root) { printf("Failed to parse project json\n"); for (int i=0;i<parts;i++) free(objs[i]); free(objs); return; }

    struct json_object *objects; json_object_object_get_ex(root, "objects", &objects);
    if (!objects) { printf("No objects in project\n"); json_object_put(root); for (int i=0;i<parts;i++) free(objs[i]); free(objs); return; }

    const char *target = objs[parts-1];
    struct json_object *tobj;
    if (!json_object_object_get_ex(objects, target, &tobj)) { printf("Target object '%s' not found\n", target); json_object_put(root); for (int i=0;i<parts;i++) free(objs[i]); free(objs); return; }

    struct json_object *titems, *thistory; json_object_object_get_ex(tobj, "items", &titems); json_object_object_get_ex(tobj, "history", &thistory);

    for (int s=0; s<parts-1; ++s) {
        struct json_object *sobj;
        if (!json_object_object_get_ex(objects, objs[s], &sobj)) { printf("Source object '%s' not found, skipping\n", objs[s]); continue; }
        struct json_object *sitems, *shistory; json_object_object_get_ex(sobj, "items", &sitems); json_object_object_get_ex(sobj, "history", &shistory);
        if (sitems && titems) {
            int len = json_object_array_length(sitems);
            for (int i=0;i<len;i++) { struct json_object *it = json_object_array_get_idx(sitems, i); json_object_array_add(titems, json_object_get(it)); }
        }
        if (shistory && thistory) {
            int len = json_object_array_length(shistory);
            for (int i=0;i<len;i++) { struct json_object *it = json_object_array_get_idx(shistory, i); json_object_array_add(thistory, json_object_get(it)); }
        }
    }

    // Write back
    FILE *fw = fopen(project_file, "w");
    if (fw) {
        fprintf(fw, "%s\n", json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
        fclose(fw);
        printf("Merged objects into %s\n", target);

        // Prompt whether to delete source objects
        printf("Delete source objects? y/N: "); fflush(stdout);
        char dresp[8];
        if (fgets(dresp, sizeof(dresp), stdin) && (dresp[0] == 'y' || dresp[0] == 'Y')) {
            for (int s = 0; s < parts-1; ++s) {
                json_object_object_del(objects, objs[s]);
            }
            // Write again after deletions
            FILE *fw2 = fopen(project_file, "w");
            if (fw2) {
                fprintf(fw2, "%s\n", json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
                fclose(fw2);
                printf("Deleted source objects and updated project file\n");
            } else {
                printf("Failed to write project file after deletions\n");
            }
        }
    } else {
        printf("Failed to write project file\n");
    }

    json_object_put(root);
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

    FILE *f = fopen(project_file, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    struct json_object *root = json_tokener_parse(content);
    free(content);
    if (!root) return;

    struct json_object *objects, *proj_name;
    json_object_object_get_ex(root, "objects", &objects);
    json_object_object_get_ex(root, "name", &proj_name);

    struct json_object *obj;
    if (!json_object_object_get_ex(objects, object_name, &obj)) {
        printf("Object '%s' not found in project '%s'\n", object_name, proj_ident);
        json_object_put(root);
        return;
    }

    struct json_object *items;
    json_object_object_get_ex(obj, "items", &items);
    int item_count = json_object_array_length(items);

    if (item_count == 0) {
        printf("\n=== %s/%s (empty) ===\n", proj_name ? json_object_get_string(proj_name) : proj_ident, object_name);
        json_object_put(root);
        return;
    }

    printf("\n=== %s/%s ===\n", proj_name ? json_object_get_string(proj_name) : proj_ident, object_name);
    for (int i = 0; i < item_count; i++) {
        struct json_object *item = json_object_array_get_idx(items, i);
        struct json_object *timestamp, *text;
        json_object_object_get_ex(item, "timestamp", &timestamp);
        json_object_object_get_ex(item, "text", &text);

        printf("%d. [%s] %s\n", i + 1,
               json_object_get_string(timestamp),
               json_object_get_string(text));
    }

    json_object_put(root);
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
    struct json_object *root = NULL;

    // If arg provided and matches a project identifier, show that project
    if (arg && get_project_file_by_ident(cfg, arg, project_file, NULL)) {
        FILE *f = fopen(project_file, "r");
        if (!f) {
            printf("Failed to open project '%s'\n", arg);
            return;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        char *content = malloc(size + 1);
        fread(content, 1, size, f);
        content[size] = '\0';
        fclose(f);

        root = json_tokener_parse(content);
        free(content);

        if (!root) {
            printf("Failed to parse project '%s'\n", arg);
            return;
        }

        struct json_object *objects, *proj_name;
        json_object_object_get_ex(root, "objects", &objects);
        json_object_object_get_ex(root, "name", &proj_name);

        if (!objects || json_object_object_length(objects) == 0) {
            printf("No objects in project '%s'\n", proj_name ? json_object_get_string(proj_name) : arg);
            json_object_put(root);
            return;
        }

        printf("\n=== Objects in '%s' ===\n", proj_name ? json_object_get_string(proj_name) : arg);
        json_object_object_foreach(objects, key, val) {
            struct json_object *items;
            json_object_object_get_ex(val, "items", &items);
            int item_count = json_object_array_length(items);
            printf("  • %s (%d items)\n", key, item_count);
        }

        json_object_put(root);
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

    FILE *f = fopen(project_file, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    root = json_tokener_parse(content);
    free(content);

    if (!root) return;

    struct json_object *objects, *proj_name;
    json_object_object_get_ex(root, "objects", &objects);
    json_object_object_get_ex(root, "name", &proj_name);

    // If no arg provided, show all objects in primary
    if (!arg) {
        if (json_object_object_length(objects) == 0) {
            printf("No objects in project '%s'\n", json_object_get_string(proj_name));
            json_object_put(root);
            return;
        }

        printf("\n=== Objects in '%s' ===\n", json_object_get_string(proj_name));
        json_object_object_foreach(objects, key, val) {
            struct json_object *items;
            json_object_object_get_ex(val, "items", &items);
            int item_count = json_object_array_length(items);
            printf("  • %s (%d items)\n", key, item_count);
        }

        json_object_put(root);
        return;
    }

    // If arg did not match a project, treat it as an object name in primary and show its items
    struct json_object *obj;
    if (!json_object_object_get_ex(objects, arg, &obj)) {
        printf("Object '%s' not found\n", arg);
        json_object_put(root);
        return;
    }

    struct json_object *items;
    json_object_object_get_ex(obj, "items", &items);
    int item_count = json_object_array_length(items);

    if (item_count == 0) {
        printf("\n=== %s (empty) ===\n", arg);
        json_object_put(root);
        return;
    }

    printf("\n=== %s ===\n", arg);
    for (int i = 0; i < item_count; i++) {
        struct json_object *item = json_object_array_get_idx(items, i);
        struct json_object *timestamp, *text;
        json_object_object_get_ex(item, "timestamp", &timestamp);
        json_object_object_get_ex(item, "text", &text);

        printf("%d. [%s] %s\n", i + 1,
               json_object_get_string(timestamp),
               json_object_get_string(text));
    }

    json_object_put(root);
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
    
    FILE *f = fopen(project_file, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);
    
    struct json_object *root = json_tokener_parse(content);
    free(content);
    
    if (!root) return;
    
    struct json_object *objects, *obj;
    json_object_object_get_ex(root, "objects", &objects);
    
    if (!json_object_object_get_ex(objects, object_name, &obj)) {
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
            json_object_put(root);
            return;
        }

        // Create the object
        json_object_put(root);
        add_object(cfg, object_name);

        // Re-load project file to pick up the newly created object
        FILE *f2 = fopen(project_file, "r");
        if (!f2) return;
        fseek(f2, 0, SEEK_END);
        long size2 = ftell(f2);
        fseek(f2, 0, SEEK_SET);
        char *content2 = malloc(size2 + 1);
        fread(content2, 1, size2, f2);
        content2[size2] = '\0';
        fclose(f2);

        struct json_object *root2 = json_tokener_parse(content2);
        free(content2);
        if (!root2) return;

        json_object_object_get_ex(root2, "objects", &objects);
        if (!json_object_object_get_ex(objects, object_name, &obj)) {
            // Strange -- object still missing
            printf("Failed to create object '%s'\n", object_name);
            json_object_put(root2);
            return;
        }

        // Replace root with root2 for subsequent writes
        root = root2;
    }
    
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    
    struct json_object *item = json_object_new_object();
    json_object_object_add(item, "timestamp", json_object_new_string(timestamp));
    json_object_object_add(item, "text", json_object_new_string(text));
    
    struct json_object *items;
    json_object_object_get_ex(obj, "items", &items);
    json_object_array_add(items, item);
    
    struct json_object *hist_entry = json_object_new_object();
    json_object_object_add(hist_entry, "action", json_object_new_string("ADD"));
    json_object_object_add(hist_entry, "timestamp", json_object_new_string(timestamp));
    json_object_object_add(hist_entry, "text", json_object_new_string(text));
    
    struct json_object *history;
    json_object_object_get_ex(obj, "history", &history);
    json_object_array_add(history, hist_entry);
    
    f = fopen(project_file, "w");
    if (f) {
        fprintf(f, "%s\n", json_object_to_json_string_ext(root, 
                JSON_C_TO_STRING_PRETTY));
        fclose(f);
        printf("Added item to %s\n", object_name);
    }
    
    json_object_put(root);
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

    FILE *f = fopen(project_file, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    struct json_object *root = json_tokener_parse(content);
    free(content);

    if (root) {
        struct json_object *name;
        json_object_object_get_ex(root, "name", &name);
        printf("Set primary project to '%s'\n", json_object_get_string(name));
        json_object_put(root);
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
        if (strstr(entry->d_name, ".json")) {
            char path[MAX_PATH];
            snprintf(path, MAX_PATH, "%s/%s", cfg->projects_dir, entry->d_name);
            
            FILE *f = fopen(path, "r");
            if (!f) continue;
            
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            char *content = malloc(size + 1);
            fread(content, 1, size, f);
            content[size] = '\0';
            fclose(f);
            
            struct json_object *root = json_tokener_parse(content);
            free(content);
            
            if (root) {
                struct json_object *idx, *name;
                json_object_object_get_ex(root, "index", &idx);
                json_object_object_get_ex(root, "name", &name);
                
                int proj_idx = json_object_get_int(idx);
                const char *proj_name = json_object_get_string(name);
                
                printf("  [%d] %s%s\n", proj_idx, proj_name, 
                       proj_idx == primary ? " (PRIMARY)" : "");
                
                json_object_put(root);
            }
        }
    }
    
    closedir(dir);
}

/* Show usage */
void show_usage(const char *prog) {
    printf("FunkNotes - Git-like note taking\n\n");
    printf("Usage:\n");
    printf("  %s new <name>              Create a new project\n", prog);
    printf("  %s primary <name|index>     Set primary project by name or index\n", prog);
    printf("  %s object <name>           Create a new object\n", prog);
    printf("  %s add <object> <text>     Add item to an object\n", prog);
    printf("  %s projects                List all projects\n", prog);
    printf("  %s show                    Show objects in primary\n", prog);
    printf("  %s show <project> <object>  Show items of an object in a specific project\n", prog);
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
    
    if (strcmp(argv[1], "new") == 0 && argc == 3) {
        new_project(&cfg, argv[2]);
    }
    else if (strcmp(argv[1], "primary") == 0 && argc == 3) {
        set_primary(&cfg, argv[2]);
    }
    else if (strcmp(argv[1], "object") == 0 && argc == 3) {
        add_object(&cfg, argv[2]);
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
        printf("Usage: funknotes add <object> <text> OR echo \"text\" | funknotes add <object>\n");
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
                FILE *f = fopen(project_file, "r");
                if (!f) { show_usage(argv[0]); }
                else {
                    fseek(f, 0, SEEK_END);
                    long size = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    char *content = malloc(size + 1);
                    fread(content, 1, size, f);
                    content[size] = '\0';
                    fclose(f);

                    struct json_object *root = json_tokener_parse(content);
                    free(content);

                    const char *maybe_obj = argv[2];
                    const char *obj_name = NULL;
                    int kw_start = 2;

                    if (root) {
                        struct json_object *objects;
                        json_object_object_get_ex(root, "objects", &objects);
                        struct json_object *tmp;
                        if (objects && json_object_object_get_ex(objects, maybe_obj, &tmp)) {
                            // first token is an object name
                            obj_name = maybe_obj;
                            kw_start = 3;
                        }
                        json_object_put(root);
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
        // Support:
        //   funknotes delete project <name|index>
        //   funknotes delete projects <proj1,proj2,...>
        //   funknotes delete object <name>
        //   funknotes delete <object> <index>   (delete specific item from object)
        if (argc == 4) {
            if (strcmp(argv[2], "project") == 0) {
                delete_project(&cfg, argv[3]);
            } else if (strcmp(argv[2], "projects") == 0) {
                // split comma-separated idents in argv[3]
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
                // If argv[2] is not a keyword, treat as: delete <object> <index>
                // Support single index or comma/range list like "1,3,5-7"
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
