#include "naming-server.h"
#include "cache.h"


// Global port
TrieNode *root;
StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
int storage_server_count = 0;
struct lru_cache *cache; // Cache pointer
sem_t storage_server_sem;                 // Semaphore to track storage servers
pthread_mutex_t storage_server_mutex;     // Mutex to protect storage_server_count

// Signal Handler to save data on exit
void signal_handler(int sig)
{
    printf("Received signal %d, saving data and exiting...\n", sig);
    save_trie("trie_data.bin");
    save_cache("cache_data.bin");
    exit(0);
}

// Create a new Trie Node
TrieNode *create_trie_node()
{
    TrieNode *node = (TrieNode *)malloc(sizeof(TrieNode));
    for (int i = 0; i < 256; i++)
        node->children[i] = NULL;
    node->file_entry = NULL;
    return node;
}

// Insert a path into the Trie
void insert_path(const char *path, int storage_server_id)
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

// Search for a path in the Trie
int search_path(const char *path)
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

// Register a Storage Server
int register_storage_server(const char *ip, int port)
{
    int id = storage_server_count++;
    storage_servers[id].id = id;
    strcpy(storage_servers[id].ip_address, ip);
    storage_servers[id].port = port;
    printf("Registered Storage Server %d: %s:%d\n", id, ip, port);
    return id;
}

// Function to save Trie to a file
void save_trie(const char *filename)
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

// Function to load Trie from a file
void load_trie(const char *filename)
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

// Function to save Cache to a file
void save_cache(const char *filename)
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

// Function to load Cache from a file
void load_cache(const char *filename)
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

void handle_storage_server(int client_socket, char *buffer)
{
    char *token = strtok(buffer, " \n");
    token = strtok(NULL, " \n"); // Get port
    int port = atoi(token);

    // Get storage server IP
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    getpeername(client_socket, (struct sockaddr *)&addr, &addr_size);
    char *ip = inet_ntoa(addr.sin_addr);

    // Register storage server
    pthread_mutex_lock(&storage_server_mutex);
    int ss_id = register_storage_server(ip, port);
    pthread_mutex_unlock(&storage_server_mutex);

    // Read paths
    while ((token = strtok(NULL, " \n")) != NULL)
    {
        if (strcmp(token, "STOP") == 0)
            break;
        insert_path(token, ss_id);
    }
}


void *handle_connection(void *arg)
{
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[1024] = {0};
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0)
    {
        close(client_socket);
        return NULL;
    }
    buffer[bytes_received] = '\0';

    // Check if the message is from a storage server
    printf("Received: %s\n", buffer);
    if (strncmp(buffer, "STORAGESERVER", 13) == 0)
    {
        handle_storage_server(client_socket, buffer);

        // If this is the first storage server, post the semaphore
        pthread_mutex_lock(&storage_server_mutex);
        if (storage_server_count == 1)
        {
            printf("At least one storage server is connected\n");
            sem_post(&storage_server_sem);
        }
        pthread_mutex_unlock(&storage_server_mutex);
    }
    else
    {
        // Wait until at least one storage server is connected
        sem_wait(&storage_server_sem);
        sem_post(&storage_server_sem); // Keep semaphore value unchanged

        handle_client(client_socket, buffer);
    }

    //close(client_socket);
    return NULL;
}





void handle_client(int client_socket, char *buffer)
{
    // Process client request
    char *token = strtok(buffer, " \n");
    if (strcmp(token, "REQUEST") == 0)
    {
        char *path = strtok(NULL, " \n");

        // Search in cache
        FileEntry *entry = cache_get(path, cache);
        if (entry != NULL)
        {
            int ss_id = entry->storage_server_id;
            char response[256];
            snprintf(response, sizeof(response), "%s %d\n",
                     storage_servers[ss_id].ip_address,
                     storage_servers[ss_id].port);
            send(client_socket, response, strlen(response), 0);
        }
        else
        {
            // Search in Trie
            int ss_id = search_path(path);
            if (ss_id != -1)
            {
                // Update Cache
                FileEntry *new_entry = malloc(sizeof(FileEntry));
                strcpy(new_entry->filename, path);
                new_entry->storage_server_id = ss_id;
                cache_put(path, new_entry, cache);

                // Send response
                char response[256];
                snprintf(response, sizeof(response), "%s %d\n",
                         storage_servers[ss_id].ip_address,
                         storage_servers[ss_id].port);
                send(client_socket, response, strlen(response), 0);
            }
            else
            {
                const char *msg = "Error: File not found\n";
                send(client_socket, msg, strlen(msg), 0);
            }
        }
    }
    else
    {
        const char *msg = "Invalid command\n";
        send(client_socket, msg, strlen(msg), 0);
    }
}

void *accept_connections(void *arg)
{
    int server_fd = *(int *)arg;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    while (1)
    {
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0)
        {
            perror("Accept");
            continue;
        }

        // Handle each connection in a new thread
        pthread_t thread_id;
        int *client_sock = malloc(sizeof(int));
        *client_sock = new_socket;
        pthread_create(&thread_id, NULL, handle_connection, (void *)client_sock);
        pthread_detach(thread_id);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: ./naming_server <port>\n");
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    char hostbuffer[256];
    gethostname(hostbuffer, sizeof(hostbuffer));

    // Get host information
    struct hostent *host_entry = gethostbyname(hostbuffer);

    // Convert the network address to a string
    char *IPbuffer = inet_ntoa(*((struct in_addr *)host_entry->h_addr_list[0]));

    printf("Naming Server is running on IP: %s, Port: %d\n", IPbuffer, port);

    // Initialize Trie
    root = create_trie_node();

    // Initialize Cache
    cache = init_cache(CACHE_SIZE);

    // Load Trie and Cache from files
    load_trie("trie_data.bin");
    load_cache("cache_data.bin");

    // Set up signal handlers to save data on exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize Naming Server
    sem_init(&storage_server_sem, 0, 1);
    pthread_mutex_init(&storage_server_mutex, NULL);
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create Socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Set Options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt)))
    {
        perror("Setsockopt");
        exit(EXIT_FAILURE);
    }

    // Bind
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Accept connections from any IP
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_fd, 10) < 0)
    {
        perror("Listen");
        exit(EXIT_FAILURE);
    }

    printf("Naming Server listening on port %d\n", port);


    pthread_t accept_thread;
    pthread_create(&accept_thread, NULL, accept_connections, (void *)&server_fd);
    // Main loop to accept and handle clients sequentially

    sem_wait(&storage_server_sem); // Wait until at least one storage server is connected
    pthread_join(accept_thread, NULL);

    sem_destroy(&storage_server_sem);
    pthread_mutex_destroy(&storage_server_mutex);

    return 0;
}
