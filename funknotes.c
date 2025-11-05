/*
 * FunkNotes - Command-line note taking in C
 * Compile: gcc -o funknotes funknotes.c -ljson-c
 * Requires: libjson-c-dev
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* Get timestamp string */
void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", t);
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

/* Show objects or items in an object */
void show(Config *cfg, const char *object_name) {
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

    // If no object name provided, show all objects
    if (!object_name) {
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
    }
    // Otherwise, show items in the specified object
    else {
        struct json_object *obj;
        if (!json_object_object_get_ex(objects, object_name, &obj)) {
            printf("Object '%s' not found\n", object_name);
            json_object_put(root);
            return;
        }

        struct json_object *items;
        json_object_object_get_ex(obj, "items", &items);
        int item_count = json_object_array_length(items);

        if (item_count == 0) {
            printf("\n=== %s (empty) ===\n", object_name);
            json_object_put(root);
            return;
        }

        printf("\n=== %s ===\n", object_name);
        for (int i = 0; i < item_count; i++) {
            struct json_object *item = json_object_array_get_idx(items, i);
            struct json_object *timestamp, *text;
            json_object_object_get_ex(item, "timestamp", &timestamp);
            json_object_object_get_ex(item, "text", &text);

            printf("%d. [%s] %s\n", i + 1,
                   json_object_get_string(timestamp),
                   json_object_get_string(text));
        }
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
        printf("Object '%s' does not exist\n", object_name);
        json_object_put(root);
        return;
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
void set_primary(Config *cfg, int index) {
    int primary, counter;
    load_config_data(cfg, &primary, &counter);
    
    char project_file[MAX_PATH];
    if (!get_project_file(cfg, index, project_file)) {
        printf("Project with index %d not found\n", index);
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
    
    save_config_data(cfg, index, counter);
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
    printf("  %s primary <index>         Set primary project\n", prog);
    printf("  %s object <name>           Create a new object\n", prog);
    printf("  %s add <object> <text>     Add item to an object\n", prog);
    printf("  %s list                    List all projects\n", prog);
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
        set_primary(&cfg, atoi(argv[2]));
    }
    else if (strcmp(argv[1], "object") == 0 && argc == 3) {
        add_object(&cfg, argv[2]);
    }
    else if (strcmp(argv[1], "show") == 0) {
       if (argc == 2) {
            show(&cfg, NULL);  // Show all objects
        } else {
            show(&cfg, argv[2]);  // Show specific object's items
        }
    }
    else if (strcmp(argv[1], "add") == 0 && argc >= 4) {
        char text[MAX_TEXT] = "";
        for (int i = 3; i < argc; i++) {
            strcat(text, argv[i]);
            if (i < argc - 1) strcat(text, " ");
        }
        add_item(&cfg, argv[2], text);
    }
    else if (strcmp(argv[1], "list") == 0) {
        list_projects(&cfg);
    }
    else {
        show_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
