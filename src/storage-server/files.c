#include "files.h"
#include "requests.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <semaphore.h>

struct file** file_entries = NULL;
unsigned long long int n_file_entries = 0;
sem_t n_file_sem; // This lock must be held when modifying the file entries.

struct file* make_file_entry(char* vpath, char* rpath, char* mtime) {
    struct file* entry = (struct file*)malloc(sizeof(struct file));
    if(entry == NULL) {
        perror("Memory allocation failed");
        exit(1);
    }
    entry->vpath = strdup(vpath);
    entry->rpath = strdup(rpath);
    entry->mtime = strdup(mtime);
    sem_init(&(entry->lock), 0, 1);
    sem_init(&(entry->writelock), 0, 1);
    sem_init(&(entry->serviceQueue), 0, 1);
    entry->readers = 0;
    return entry;
}

struct trie_node* trieRoot = NULL;

struct trie_node* create_trie_node() {
    // Creates a null trie node.
    struct trie_node *node = (struct trie_node*)malloc(sizeof(struct trie_node));
    for(int i = 0; i < 256; i++) node->children[i] = NULL;
    node->file = NULL;
    return node;
}

struct file* add_file_entry(char* vpath, char* rpath, char* mtime, bool toFile) {
    // Adds a file entry to the trie for fast access.
    if(!trieRoot) trieRoot = create_trie_node();
    struct trie_node *current = trieRoot;
    struct file* file = make_file_entry(vpath, rpath, mtime);
    for(int i = 0; vpath[i]; i++) {
        unsigned char index = (unsigned char)vpath[i];
        if(!current->children[index]) current->children[index] = create_trie_node();
        current = current->children[index];
    }
    if(!current->file) current->file = file;
    else return NULL;
    sem_wait(&n_file_sem);
    file_entries = (struct file**)realloc(file_entries, sizeof(struct file*) * (n_file_entries + 1));
    file_entries[n_file_entries++] = file;
    sem_post(&n_file_sem);
    if(toFile) {
        FILE* pathsfile = fopen("paths.txt", "a");
        fprintf(pathsfile, "%s %s %s\n", vpath, rpath, mtime);
        fclose(pathsfile);
    }
    return file;
}

struct file* get_file(char* vpath) {
    // Returns the pointer to the file entry corresponding to the given virtual path.
    if(!trieRoot) trieRoot = create_trie_node();
    struct trie_node *current = trieRoot;
    for(int i = 0; vpath[i]; i++) {
        unsigned char index = (unsigned char)vpath[i];
        if(current->children[index] == NULL) return NULL;
        current = current->children[index];
    }
    if(current) return current->file;
    else return NULL;
}

int remove_file_entry(char* vpath) {
    // Remove a file entry from the trie.
    if (!trieRoot) trieRoot = create_trie_node();
    struct trie_node *current = trieRoot;
    for (int i = 0; vpath[i]; i++) {
        unsigned char index = (unsigned char)vpath[i];
        if (!current->children[index]) return 1;
        current = current->children[index];
    }
    if (!current->file) return 1;
    else {
        // Open paths.txt in read-write mode
        FILE *file = fopen("paths.txt", "r+");
        if (!file) {
            perror("Error opening paths.txt");
            return 1;
        }

        // long pos = 0;
        char line[MAXPATHLENGTH * 3]; // Adjust buffer size as needed
        bool found = false;
        while (fgets(line, sizeof(line), file)) {
            // Keep track of the position of the line
            long line_pos = ftell(file) - strlen(line);
            // Parse the line to get vpath
            char file_vpath[MAXPATHLENGTH + 1];
            char file_rpath[MAXPATHLENGTH + 1];
            char file_mtime[MAXPATHLENGTH + 1];
            if (sscanf(line, "%s %s %s", file_vpath, file_rpath, file_mtime) == 3) {
                if (strcmp(file_vpath, vpath) == 0) {
                    // Found the line to delete
                    found = true;
                    // Move the file pointer to the beginning of the line
                    fseek(file, line_pos, SEEK_SET);
                    // Overwrite the line with spaces or blank line
                    int line_length = strlen(line);
                    for (int i = 0; i < line_length - 1; i++) { // -1 to leave the newline character
                        fputc(' ', file);
                    }
                    fputc('\n', file); // Ensure the newline character remains
                    break;
                }
            }
            // pos = ftell(file);
        }
        fclose(file);
        if (!found) {
            fprintf(stderr, "Error: Entry not found in paths.txt\n");
            return 1;
        }

        // Remove from file_entries array
        for (unsigned long long int i = 0; i < n_file_entries; i++) {
            if (strcmp(file_entries[i]->vpath, vpath) == 0) {
                // Shift the remaining entries
                sem_wait(&n_file_sem);
                file_entries[i] =file_entries[n_file_entries - 1];
                file_entries = realloc(file_entries, sizeof(struct file*) * (n_file_entries - 1));
                n_file_entries--;
                sem_post(&n_file_sem);
                break;
            }
        }
        // Clean up the file structure
        sem_destroy(&(current->file->lock));
        sem_destroy(&(current->file->writelock));
        sem_destroy(&(current->file->serviceQueue));
        free(current->file->vpath);
        free(current->file->rpath);
        free(current->file->mtime);
        free(current->file);
        current->file = NULL;

        return 0;
    }
}