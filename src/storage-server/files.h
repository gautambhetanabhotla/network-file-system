#include <pthread.h>
#include <stdbool.h>
#include <semaphore.h>

#ifndef FILE_H
#define FILE_H

struct trie_node {
    struct file* file;
    struct trie_node* children[256];
};

extern struct trie_node* trieRoot;

struct file {
    char *vpath, *rpath;
    sem_t lock, writelock, serviceQueue;
    int readers;
};

int add_file_entry(char* vpath, char* rpath, bool toFile);
struct file* get_file(char* vpath);

#endif