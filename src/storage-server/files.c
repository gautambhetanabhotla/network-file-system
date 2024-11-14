#include "storage-server.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct file** file_entries = NULL;
int n_file_entries = 0;

struct file* make_file_entry(char* vpath, char* rpath) {
    struct file* entry = (struct file*)malloc(sizeof(struct file));
    if(entry == NULL) {
        perror("Memory allocation failed");
        exit(1);
    }
    entry->vpath = strdup(vpath);
    entry->rpath = strdup(rpath);
    entry->writelock = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    return entry;
}

void add_file_entry(char* vpath, char* rpath) {
    struct file* entry = make_file_entry(vpath, rpath);
    file_entries = realloc(file_entries, sizeof(struct file*) * (n_file_entries + 1));
    file_entries[n_file_entries++] = entry;
}