#ifndef TRIE_H
#define TRIE_H

#include "trie.h"
#include "cache.h"
#include "naming-server.h"

// Trie Node Structure
typedef struct TrieNode {
    struct TrieNode *children[256];
    FileEntry *file_entry;
} TrieNode;

TrieNode *create_trie_node();
void insert_path(const char *path, int* storage_server_ids, TrieNode *root);
int search_path(const char *path, TrieNode *root);
void save_trie(const char *filename, TrieNode *root);
void load_trie(const char *filename, TrieNode *root);
void remove_path(const char *path, TrieNode *root);

#endif