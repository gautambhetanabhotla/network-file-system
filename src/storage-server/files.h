#include <pthread.h>
#include <stdbool.h>
#include <semaphore.h>
#include <time.h>

#ifndef FILE_H
#define FILE_H

#define _GNU_SOURCE

struct trie_node {
    struct file* file;
    struct trie_node* children[256];
};

extern struct trie_node* trieRoot;

struct file {
    char *vpath, *rpath, *mtime;
    sem_t lock, writelock, serviceQueue;
    int readers;
};

struct file* add_file_entry(char* vpath, char* rpath, char* mtime, bool toFile);
struct file* get_file(char* vpath);

#endif