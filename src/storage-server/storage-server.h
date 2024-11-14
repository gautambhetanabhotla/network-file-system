#include <pthread.h>

#ifndef STORAGE_SERVER_H
#define STORAGE_SERVER_H

struct file {
    char *vpath, *rpath;
    pthread_mutex_t writelock;
};

struct file* make_file_entry(char* vpath, char* rpath);
void add_file_entry(char* vpath, char* rpath);

void *ss_read(void* arg), *ss_write(void* arg), *ss_create(void* arg), *ss_delete(void* arg), *ss_stream(void* arg);

#endif