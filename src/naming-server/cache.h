#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "naming-server.h"

#define MAX_LEN 1024
#define CACHE_MAX_SIZE 10

typedef char KEY_TYPE[MAX_LEN];

struct lru_cache_node {
    KEY_TYPE key;
    FileEntry* value;
    struct lru_cache_node *prev;
    struct lru_cache_node *next;
};

//random comment
struct lru_cache {
    int size;
    int max_size;
    struct lru_cache_node *most_recently_used;
    struct lru_cache_node *least_recently_used;
};

FileEntry* cache_get(KEY_TYPE key, struct lru_cache *cache);
FileEntry* cache_put(KEY_TYPE key, FileEntry* value, struct lru_cache *cache);
void save_cache(const char *filename, struct lru_cache *cache);
void load_cache(const char *filename, struct lru_cache *cache);
struct lru_cache* init_cache(int max_size);

#endif