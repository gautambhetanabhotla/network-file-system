#include "trie.h"


void insert_path(const char *path, int storage_server_id, TrieNode *root)
{
    TrieNode *current = root;
    for (int i = 0; path[i]; i++)
    {
        unsigned char index = (unsigned char)path[i];
        if (!current->children[index])
            current->children[index] = create_trie_node();
        current = current->children[index];
    }
    if (!current->file_entry)
    {
        current->file_entry = (FileEntry *)malloc(sizeof(FileEntry));
        strcpy(current->file_entry->filename, path);
        current->file_entry->storage_server_id = storage_server_id;
        current->file_entry->is_copy = NULL;
    }
}

int search_path(const char *path, TrieNode *root)
{
    TrieNode *current = root;
    for (int i = 0; path[i]; i++)
    {
        unsigned char index = (unsigned char)path[i];
        if (!current->children[index])
            return -1; // Not found
        current = current->children[index];
    }
    if (current->file_entry)
        return current->file_entry->storage_server_id;
    return -1; // Not found
}

void save_trie(const char *filename, TrieNode *root)
{
    FILE *file = fopen(filename, "wb");
    if (!file)
    {
        perror("Failed to open trie data file for writing");
        return;
    }
    // Recursively save trie nodes
    void save_node(TrieNode * node, FILE * file)
    {
        if (!node)
            return;
        // Write a flag indicating if the node has a FileEntry
        uint8_t has_file_entry = (node->file_entry != NULL);
        fwrite(&has_file_entry, sizeof(uint8_t), 1, file);
        if (has_file_entry)
        {
            // Write the FileEntry data
            fwrite(node->file_entry, sizeof(FileEntry), 1, file);
        }
        // Recursively save children
        for (int i = 0; i < 256; i++)
        {
            uint8_t has_child = (node->children[i] != NULL);
            fwrite(&has_child, sizeof(uint8_t), 1, file);
            if (has_child)
            {
                save_node(node->children[i], file);
            }
        }
    }
    save_node(root, file);
    fclose(file);
}

void load_trie(const char *filename, TrieNode *root)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        printf("Trie data file not found, starting with empty trie.\n");
        return;
    }
    // Recursively load trie nodes
    printf("Loading Trie from file...\n");
    void load_node(TrieNode * node, FILE * file)
    {
        uint8_t has_file_entry;
        fread(&has_file_entry, sizeof(uint8_t), 1, file);
        if (has_file_entry)
        {
            node->file_entry = (FileEntry *)malloc(sizeof(FileEntry));
            fread(node->file_entry, sizeof(FileEntry), 1, file);
            printf("Loaded file entry: %s\n", node->file_entry->filename);
        }
        for (int i = 0; i < 256; i++)
        {
            uint8_t has_child;
            fread(&has_child, sizeof(uint8_t), 1, file);
            if (has_child)
            {
                node->children[i] = create_trie_node();
                load_node(node->children[i], file);
            }
        }
    }
    load_node(root, file);
    fclose(file);
}


TrieNode *create_trie_node()
{
    TrieNode *node = (TrieNode *)malloc(sizeof(TrieNode));
    for (int i = 0; i < 256; i++)
        node->children[i] = NULL;
    node->file_entry = NULL;
    return node;
}