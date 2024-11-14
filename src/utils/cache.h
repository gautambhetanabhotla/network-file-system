#ifndef LRU_CACHE_H
#define LRU_CACHE_H

typedef int KEY_TYPE;
typedef int VALUE_TYPE;

struct lru_cache_node {
    KEY_TYPE key;
    VALUE_TYPE value;
    struct lru_cache_node *prev;
    struct lru_cache_node *next;
};

struct lru_cache {
    int size;
    int max_size;
    struct lru_cache_node *most_recently_used;
    struct lru_cache_node *least_recently_used;
};

VALUE_TYPE cache_get(KEY_TYPE key, struct lru_cache *cache);

#endif