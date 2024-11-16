#include "files.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <semaphore.h>

struct file** file_entries = NULL;
unsigned long long int n_file_entries = 0;
sem_t n_file_sem;

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
    // Removes a file entry from the trie.
    if(!trieRoot) trieRoot = create_trie_node();
    struct trie_node *current = trieRoot;
    for(int i = 0; vpath[i]; i++) {
        unsigned char index = (unsigned char)vpath[i];
        if(!current->children[index]) return 1;
        current = current->children[index];
    }
    if(!current->file) return 1;
    else {
        sem_destroy(&(current->file->lock));
        sem_destroy(&(current->file->writelock));
        sem_destroy(&(current->file->serviceQueue));
        free(current->file->vpath);
        free(current->file->rpath);
        free(current->file);
        current->file = NULL;
        return 0;
    }
}