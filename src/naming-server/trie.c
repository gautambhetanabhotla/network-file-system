#include "main.h"

TrieNode *create_trie_node()
{
    TrieNode *node = (TrieNode *)malloc(sizeof(TrieNode));
    if (node)
    {
        node->file_entry = NULL;
        for (int i = 0; i < 256; i++)
        {
            node->children[i] = NULL;
        }
    }
    return node;
}

FileEntry* insert_path(const char *path, int *storage_server_ids, int num_chosen, TrieNode *root)
{
    // need to make sure that when a file/directory is added, the parent is also a directory
    TrieNode *current = root;
    if(strlen(path)==1){
        fprintf(stderr, "only one character, is it root?\n");
        if(path[0]=='/'){
            fprintf(stderr, "omg its root you go girl!\n");
            return root->file_entry;
        }
    }else{
        if(path[0]!='/'){
            fprintf(stderr, "path does not start with /\n");
            return NULL;
        }
    }
    for (int i = 1; path[i]; i++)
    {
        if(current == NULL){
            return NULL;
        }
        if(current->file_entry && (current->file_entry->is_folder==0))
        {
            return NULL;
        }
        unsigned char index = (unsigned char)path[i];
        // need to check if the parent is a directory
        if (!current->children[index]){
            current->children[index] = create_trie_node();
            if(index=='/'){
                current->children[index]->file_entry = (FileEntry *)malloc(sizeof(FileEntry));
                strcpy(current->children[index]->file_entry->filename, "/");
                current->children[index]->file_entry->is_folder = 1;
            }
        }
        current = current->children[index];
    }

    if (current->file_entry)
    {
        // File already exists
        return current->file_entry;
    }
    if (!current->file_entry)
    {
        current->file_entry = (FileEntry *)malloc(sizeof(FileEntry));
        strcpy(current->file_entry->filename, path);
        fprintf(stderr, "path: %s\n", current->file_entry->filename);

        for (int i = 0; i < num_chosen; i++)
        {
            fprintf(stderr, "storage server id: %d\n", storage_server_ids[i]);
            current->file_entry->ss_ids[i] = storage_server_ids[i];
            storage_servers[storage_server_ids[i]].file_count++;
            fprintf(stderr, "file count: %d\n", storage_servers[storage_server_ids[i]].file_count);
        }
    }

    return current->file_entry;
}

void set_file_entry_timestamp(FileEntry *file, const char *timestamp)
{
    strcpy(file->last_modified, timestamp);
}

FileEntry* search_path(const char *path, TrieNode *root)
{
    if (path[strlen(path) - 1] != '/')
    {
        // means it is a folder
    }
    TrieNode *current = root;
    if(strlen(path)==1){
        fprintf(stderr, "only one character, is it root?\n");
        if(path[0]=='/'){
            fprintf(stderr, "omg its root you go girl!\n");
            return root->file_entry;
        }
    }
    for (int i = 1; path[i]; i++)
    {
        unsigned char index = (unsigned char)path[i];
        if (!current->children[index])
            return NULL; // Not found
        current = current->children[index];
    }
    if (current->file_entry)
        return current->file_entry;
    return NULL; // Not found
}

// implement a search folder function similar to search_path
// TrieNode* search_folder(const char *path, TrieNode *root)
// {
//     TrieNode *current = root;
//     if (path[strlen(path) - 1] != '/')
//     {
//         return NULL;
//     }
//     if(strlen(path)==1){
//         fprintf(stderr, "only one character, is it root?\n");
//         if(path[0]=='/'){
//             fprintf(stderr, "omg its root you go girl!\n");
//             return root->file_entry;
//         }
//     }
//     for (int i = 1; path[i]; i++)
//     {
//         unsigned char index = (unsigned char)path[i];
//         if (!current->children[index])
//             return NULL; // Not found
//         current = current->children[index];
//     }
//     if(current->file_entry && (current->file_entry->is_folder==1))
//         return current;
//     return NULL;
// }

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

void save_trie(const char *filename, TrieNode *root)
{
    FILE *file = fopen(filename, "wb");
    if (!file)
    {
        perror("Failed to open trie data file for writing");
        return;
    }
    // Recursively save trie nodes
    
    save_node(root, file);
    fclose(file);
}
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
    
    load_node(root, file);
    fclose(file);
}

// Remove a path from the Trie
void remove_path(const char *path, TrieNode *root) {
    TrieNode *current = root;
    //TrieNode *parent = NULL;
    //unsigned char last_index = 0;

    // Stack to keep track of the path
    TrieNode *node_stack[MAX_PATH_LENGTH];
    int index_stack[MAX_PATH_LENGTH];
    int depth = 0;

    for (int i = 0; path[i]; i++) {
        unsigned char index = (unsigned char)path[i];
        if (!current->children[index])
            return;  // Path not found

        node_stack[depth] = current;
        index_stack[depth] = index;
        depth++;

        current = current->children[index];
    }

    if (current->file_entry) {
        free(current->file_entry);
        current->file_entry = NULL;

        // Remove nodes if they are empty
        for (int i = depth - 1; i >= 0; i--) {
            TrieNode *node = node_stack[i]->children[index_stack[i]];
            if (node->file_entry)
                break;
            int has_children = 0;
            for (int j = 0; j < 256; j++) {
                if (node->children[j]) {
                    has_children = 1;
                    break;
                }
            }
            if (has_children)
                break;
            free(node);
            node_stack[i]->children[index_stack[i]] = NULL;
        }
    }
}

