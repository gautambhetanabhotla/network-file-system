#include "cache.h"

VALUE_TYPE cache_get(KEY_TYPE key, struct lru_cache* cache) {
    // Look for an entry in the cache and return its value.
    // If it is not present, add it and return it.
    // If the cache's size exceeds its maximum size, evict the least recently used element.
    struct lru_cache_node* current = cache->most_recently_used;

    //search for the key in cache 
    while(current != NULL) {
        if(current->key == key) {
            //move the node to the front of the list
            if(current != cache->most_recently_used) {
                if(current->prev) {
                    current->prev->next = current->next;
                }

                if(current->next) {
                    current->next->prev = current->prev;
                } 
                if(current == cache->least_recently_used) {
                    cache->least_recently_used = current->prev;
                }
                current->next = cache->most_recently_used;
                current->prev = NULL;
                if(cache->most_recently_used) {
                    cache->most_recently_used->prev = current;
                }
                cache->most_recently_used = current;
            }
            return current->value;
        }
        current = current->next;
    }
    

    //if the key is not found in the cache
    struct lru_cache_node* new_node = (struct lru_cache_node*)malloc(sizeof(struct lru_cache_node));
    new_node->key = key;
    new_node->value = 0; // default 
    new_node->next = cache->most_recently_used;
    new_node->prev = NULL;
    if(cache->most_recently_used) {
        cache->most_recently_used->prev = new_node;
    }
    cache->most_recently_used = new_node;
    if(cache->least_recently_used == NULL) {
        cache->least_recently_used = new_node;
    }
    cache->size++;

    //if the cache size exceeds the max size, evict the least recently used element
    if(cache->size > cache->max_size) {
        struct lru_cache_node* lru  = cache->least_recently_used;
        if(lru->prev)
        {
            lru->prev->next = NULL;
        }
        cache->least_recently_used = lru->prev;
        cache->least_recently_used->next = NULL;
        free(lru);
        cache->size--;
    }

    return new_node->value;
}