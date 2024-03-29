#include "build_constants.h"
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

struct duplicate_entry {
    char name[FILENAME_COMPONENT_LEN + 1];
    struct directory_node *parent;
};

struct filesize_node {
    bool overfull;
    size_t size;
    unsigned int num_dups;
    struct duplicate_entry dups[MAX_TRACKED_DUPLICATES];
};

struct hashtable_node {
    size_t num_nodes;
    struct filesize_node filesize_nodes[FILESIZES_PER_HASHTABLE_NODE];
    struct hashtable_node *next;
};

struct directory_node {
    struct directory_node *parent;
    char name[FILENAME_COMPONENT_LEN + 1];
};

int filesize_node_comparator(const void *a, const void *b) {
    const struct filesize_node *a_ptr = *(const struct filesize_node **)a;
    const struct filesize_node *b_ptr = *(const struct filesize_node **)b;
    return a_ptr->size < b_ptr->size ? -1 : 1;
}

void print_dup(const struct duplicate_entry *duplicate_entry) {
    size_t num_elements = 0;
    const char *elements[MAX_DIRECTORY_DEPTH];
    elements[num_elements++] = duplicate_entry->name;

    struct directory_node *dir = duplicate_entry->parent;
    do {
        elements[num_elements++] = dir->name;
    } while ((dir = dir->parent) && num_elements < MAX_DIRECTORY_DEPTH);

    if (num_elements == MAX_DIRECTORY_DEPTH) {
        printf("File too deeply nested in directory: ");
    }

    for (size_t i = 0; i < num_elements; ++i) {
        const char *element = elements[num_elements - 1 -i];
        if (*element == '/') {
            ++element;
        }
        printf("/%s", element);
    }
    printf("\n");
}

const char *get_base_name(const char *path) {
    const char *res = path;
    const char *ptr = path;
    while (*ptr++) {
        if (*ptr == '/') {
            res = ptr + 1;
        }
    }

    return res;
}

void handle_file(struct hashtable_node *hashtable, struct directory_node *parent_node, char *path) {
    struct stat file_info;
    int res;
    if ((res = stat(path, &file_info)) == -1) {
        printf("Bad stat\n");
        free(path);
        return;
    }

    size_t size = file_info.st_size;
    if (size < 1024) {
        return;
    }
    size_t hashtable_idx = size % HASHTABLE_BUCKETS;
    struct hashtable_node *table_node = hashtable + hashtable_idx;

    while (true) {
        for (size_t i = 0; i < table_node->num_nodes; ++i) {
            if (table_node->filesize_nodes[i].size == size) {
                if (table_node->filesize_nodes[i].num_dups == MAX_TRACKED_DUPLICATES) {
                    if (!table_node->filesize_nodes[i].overfull){
                        printf("Too many dups of size %zu\n", size);
                        table_node->filesize_nodes[i].overfull = true;
                    }
                    return;
                }
                size_t dup_idx = table_node->filesize_nodes[i].num_dups++;
                table_node->filesize_nodes[i].dups[dup_idx].parent = parent_node;
                const char *base_name = get_base_name(path);
                strncpy(table_node->filesize_nodes[i].dups[dup_idx].name, base_name, FILENAME_COMPONENT_LEN);
                return;
            }
        }
        if (!table_node->next) {
            break;
        }
        table_node = table_node->next;
    }

    if (table_node->num_nodes == FILESIZES_PER_HASHTABLE_NODE) {
        table_node->next = calloc(1, sizeof(struct hashtable_node));
        table_node = table_node->next;
    }

    size_t size_idx = table_node->num_nodes++;
    table_node->filesize_nodes[size_idx].size = size;
    table_node->filesize_nodes[size_idx].num_dups = 1;
    table_node->filesize_nodes[size_idx].dups[0].parent = parent_node;
    strncpy(table_node->filesize_nodes[size_idx].dups[0].name, get_base_name(path), 256);
}

void handle_directory(struct hashtable_node *hashtable, struct directory_node *root_node, const char *root) {
    size_t root_name_len = strlen(root);
    DIR *top = opendir(root);
    if (!top) {
        printf("Couldn't open dir %s\n", root);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(top))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        size_t name_len = strlen(entry->d_name);
        char *path = malloc((root_name_len + name_len + 1 + 1) * sizeof(char));
        strcpy(path, root);
        path[root_name_len] = '/';
        path[root_name_len + 1] = '\0';
        strcat(path, entry->d_name);

        if (entry->d_type == DT_REG) {
            handle_file(hashtable, root_node, path);
        } else if (entry->d_type == DT_DIR) {
            struct directory_node *new_node = calloc(1, sizeof(struct directory_node));
            new_node->parent = root_node;
            strncpy(new_node->name, entry->d_name, 256);
            handle_directory(hashtable, new_node, path);
            free(path);
        } else {
            printf("Unhandled file type: %s\n", entry->d_name);
        }
    }

    closedir(top);
}

void collect_filesize_nodes(struct hashtable_node *hashtable, uint_fast32_t num_hashtable_nodes,
                            struct filesize_node ***out_buffer, uint_fast32_t *out_num_filesize_nodes)
{
    struct filesize_node **result = malloc(HASHTABLE_BUCKETS * HASHTABLE_BUCKETS * sizeof(struct filesize_node *));
    uint_fast32_t result_num_nodes = 0;
    if (result == NULL) {
        *out_buffer = NULL;
        return;
    }

    for (size_t i = 0; i < num_hashtable_nodes; ++i) {
        struct hashtable_node *hashtable_node = &hashtable[i];
        while (hashtable_node) {
            for (size_t j = 0; j < hashtable_node->num_nodes; ++j) {
                struct filesize_node *filesize_node = &hashtable_node->filesize_nodes[j];
                result[result_num_nodes++] = filesize_node;
            }

            hashtable_node = hashtable_node->next;
        }
    }

    *out_buffer = result;
    *out_num_filesize_nodes = result_num_nodes;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("One argument: root file\n");
        return 1;
    }

    struct stat root;
    int res;
    if ((res = stat(argv[1], &root)) == -1) {
        printf("Error\n");
        return 1;
    }

    if ((root.st_mode & S_IFDIR) == 0) {
         printf("Not a directory\n");
         return 1;
    }

    struct hashtable_node *hashtable =
        calloc(HASHTABLE_BUCKETS, sizeof(struct hashtable_node));

    struct directory_node root_node;
    root_node.parent = NULL;
    strncpy(root_node.name, argv[1], 256);

    handle_directory(hashtable, &root_node, argv[1]);

    uint_fast32_t num_filesize_entries;
    struct filesize_node **buffer;
    collect_filesize_nodes(hashtable, HASHTABLE_BUCKETS, &buffer, &num_filesize_entries);
    if (buffer == NULL) {
        printf("Failed to collect entries\n");
        return 1;
    }
    qsort(buffer, num_filesize_entries, sizeof(struct filesize_node *), filesize_node_comparator);

    for (uint_fast32_t i = 0; i < num_filesize_entries; ++i) {
        struct filesize_node *filesize_node = buffer[i];
        if (filesize_node->num_dups > 1) {
            printf("With size %lu:", filesize_node->size);
            if (filesize_node->overfull) {
                printf(" over %uc duplicates of this size!\n", MAX_TRACKED_DUPLICATES);
            }
            else {
                printf("\n");
            }
            for (size_t j = 0; j < filesize_node->num_dups; ++j) {
                print_dup(&filesize_node->dups[j]);
            }
        }
    }

    return 0;
}
