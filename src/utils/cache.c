#include "cache.h"

VALUE_TYPE cache_get(KEY_TYPE key, struct lru_cache* cache) {
    // Look for an entry in the cache and return its value.
    // If it is not present, add it and return it.
    // If the cache's size exceeds its maximum size, evict the least recently used element.
}