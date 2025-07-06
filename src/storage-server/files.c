#include "files.h"
#include "storageserver.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <semaphore.h>
#include <unistd.h>

struct file** file_entries = NULL;
unsigned long long int n_file_entries = 0;
sem_t n_file_sem; // This lock must be held when modifying the file entries.

/**
 * Creates a file entry.
 * @param vpath The virtual path of the file.
 * @param rpath The real path of the file, that is, the path on the storage server.
 * @param mtime The last modification time of the file.
 * @return A pointer to the newly created file entry.
 */
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

/**
 * Adds a file entry to the trie for fast access.
 * @param vpath The virtual path of the file.
 * @param rpath The real path of the file.
 * @param mtime The last modification time of the file.
 * @param toFile If true, the entry is also added to paths.txt.
 * Returns a pointer to the file entry if successful, NULL if the entry already exists.
 */
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
    // TODO: actually remove it from the trie.
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

        char line[(MAXPATHLENGTH + 1) * 3]; // Adjust buffer size as needed
        long line_pos = -1;
        long last_line_pos = -1;
        char last_line[(MAXPATHLENGTH + 1) * 3];
        bool found = false;

        while (fgets(line, sizeof(line), file)) {
            long current_pos = ftell(file) - strlen(line);
            if (strcmp(line, "\n") != 0) {
                last_line_pos = current_pos;
                strcpy(last_line, line);
            }
            char file_vpath[MAXPATHLENGTH + 1];
            char file_rpath[MAXPATHLENGTH + 1];
            char file_mtime[MAXPATHLENGTH + 1];
            if (sscanf(line, "%s %s %s", file_vpath, file_rpath, file_mtime) == 3) {
                if (strcmp(file_vpath, vpath) == 0) {
                    found = true;
                    line_pos = current_pos;
                }
            }
        }

        if (!found) {
            fprintf(stderr, "Error: Entry not found in paths.txt\n");
            fclose(file);
            return 1;
        }

        if (line_pos == last_line_pos) {
            // If the line to delete is the last line, just truncate the file
            fseek(file, line_pos, SEEK_SET);
            ftruncate(fileno(file), line_pos);
        }
        else {
            // Replace the line to delete with the last line
            fseek(file, line_pos, SEEK_SET);
            fputs(last_line, file);
            // Truncate the file to remove the last line
            ftruncate(fileno(file), last_line_pos);
        }

        fclose(file);

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