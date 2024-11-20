#include "main.h"

struct lru_cache *init_cache(int max_size)
{
    struct lru_cache *cache = (struct lru_cache *)malloc(sizeof(struct lru_cache));
    cache->size = 0;
    cache->max_size = max_size;
    cache->most_recently_used = NULL;
    cache->least_recently_used = NULL;
    return cache;
}

FileEntry* cache_get(KEY_TYPE key, struct lru_cache *cache)
{
    // Look for an entry in the cache and return its value.
    // If it is not present, add it and return it.
    // If the cache's size exceeds its maximum size, evict the least recently used element.
    struct lru_cache_node *current = cache->most_recently_used;

    // search for the key in cache
    while (current != NULL)
    {
        if (strcmp(current->key, key) == 0)
        {
            // move the node to the front of the list
            if (current != cache->most_recently_used)
            {
                if (current->prev)
                {
                    current->prev->next = current->next;
                }

                if (current->next)
                {
                    current->next->prev = current->prev;
                }
                if (current == cache->least_recently_used)
                {
                    cache->least_recently_used = current->prev;
                }
                current->next = cache->most_recently_used;
                current->prev = NULL;
                if (cache->most_recently_used)
                {
                    cache->most_recently_used->prev = current;
                }
                cache->most_recently_used = current;
            }
            return current->value;
        }
        current = current->next;
    }
    return NULL;
}

FileEntry* cache_put(KEY_TYPE key, FileEntry* value, struct lru_cache *cache)
{

    // if the key is not found in the cache
    struct lru_cache_node *new_node = (struct lru_cache_node *)malloc(sizeof(struct lru_cache_node));
    strcpy(new_node->key, key);
    new_node->value = value;
    new_node->next = cache->most_recently_used;
    new_node->prev = NULL;
    if (cache->most_recently_used)
    {
        cache->most_recently_used->prev = new_node;
    }
    cache->most_recently_used = new_node;
    if (cache->least_recently_used == NULL)
    {
        cache->least_recently_used = new_node;
    }
    cache->size++;

    // if the cache size exceeds the max size, evict the least recently used element
    if (cache->size > cache->max_size)
    {
        struct lru_cache_node *lru = cache->least_recently_used;
        if (lru->prev)
        {
            lru->prev->next = NULL;
        }
        cache->least_recently_used = lru->prev;
        if(cache->least_recently_used)
        {
            cache->least_recently_used->next = NULL;
        }
        free(lru);
        cache->size--;
    }

    return new_node->value;
}


// Function to load Cache from a file
void load_cache(const char *filename, struct lru_cache *cache)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        printf("Cache data file not found, starting with empty cache.\n");
        return;
    }
    // Read the cache size
    fread(&cache->size, sizeof(int), 1, file);
    cache->most_recently_used = NULL;
    cache->least_recently_used = NULL;
    int count = cache->size;
    cache->size = 0; // Will update size as we add entries
    for (int i = 0; i < count; i++)
    {
        // Read the key
        uint32_t key_len;
        fread(&key_len, sizeof(uint32_t), 1, file);
        char key[MAX_PATH_LENGTH];
        fread(key, sizeof(char), key_len, file);
        key[key_len] = '\0';
        // Read the FileEntry
        FileEntry *value = (FileEntry *)malloc(sizeof(FileEntry));
        fread(value, sizeof(FileEntry), 1, file);
        // Put into cache
        cache_put(key, value, cache);
    }
    fclose(file);
}


// Function to save Cache to a file
void save_cache(const char *filename, struct lru_cache *cache)
{
    FILE *file = fopen(filename, "wb");
    if (!file)
    {
        perror("Failed to open cache data file for writing");
        return;
    }
    // Write the cache size
    fwrite(&cache->size, sizeof(int), 1, file);
    // Iterate through the cache and save entries
    struct lru_cache_node *current = cache->most_recently_used;
    while (current)
    {
        // Write the key length and key
        uint32_t key_len = strlen(current->key);
        fwrite(&key_len, sizeof(uint32_t), 1, file);
        fwrite(current->key, sizeof(char), key_len, file);
        // Write the FileEntry
        fwrite(current->value, sizeof(FileEntry), 1, file);
        current = current->next;
    }
    fclose(file);
}

// Cache Remove Function
void cache_remove(const char *key, struct lru_cache *cache) {
    struct lru_cache_node *current = cache->most_recently_used;

    while (current) {
        if (strcmp(current->key, key) == 0) {
            // Remove node from list
            if (current->prev)
                current->prev->next = current->next;
            else
                cache->most_recently_used = current->next;

            if (current->next)
                current->next->prev = current->prev;
            else
                cache->least_recently_used = current->prev;

            free(current->value);
            free(current);
            cache->size--;
            return;
        }
        current = current->next;
    }
}


