#include "main.h"

// Global port
TrieNode *root;
StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
int storage_server_count = 0;
struct lru_cache *cache; // Cache pointer
sem_t storage_server_sem;                 // Semaphore to track storage servers
pthread_mutex_t storage_server_mutex;     // Mutex to protect storage_server_count

// Signal Handler to save data on exit


void choose_least_full_servers(int *chosen_servers)
{
    int min_file_counts[3] = {__INT_MAX__, __INT_MAX__, __INT_MAX__};
    for (int i = 0; i < storage_server_count; i++)
    {
        int file_count = storage_servers[i].file_count;
        if (file_count < min_file_counts[0])
        {
            min_file_counts[2] = min_file_counts[1];
            chosen_servers[2] = chosen_servers[1];
            min_file_counts[1] = min_file_counts[0];
            chosen_servers[1] = chosen_servers[0];
            min_file_counts[0] = file_count;
            chosen_servers[0] = i;
        }
        else if (file_count < min_file_counts[1])
        {
            min_file_counts[2] = min_file_counts[1];
            chosen_servers[2] = chosen_servers[1];
            min_file_counts[1] = file_count;
            chosen_servers[1] = i;
        }
        else if (file_count < min_file_counts[2])
        {
            min_file_counts[2] = file_count;
            chosen_servers[2] = i;
        }
    }
}


void signal_handler(int sig)
{
    printf("Received signal %d, saving data and exiting...\n", sig);
    save_trie("trie_data.bin", root);
    save_cache("cache_data.bin", cache);
    exit(0);
}

// Register a Storage Server
int register_storage_server(const char *ip, int port)
{
    int id = storage_server_count++;
    storage_servers[id].id = id;
    strcpy(storage_servers[id].ip_address, ip);
    storage_servers[id].port = port;
    storage_servers[id].file_count = 0;
    printf("Registered Storage Server %d: %s:%d\n", id, ip, port);
    return id;
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
        
        int chosen_servers[3];
        choose_least_full_servers(chosen_servers);
        insert_path(token, chosen_servers,root);
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

//Handle Client Requests
void handle_client(int client_socket, char *initial_buffer) {
    char buffer[1024];
    strcpy(buffer, initial_buffer);  // Copy the initial buffer

    // Parse the command and arguments
    char *saveptr;
    char *command = strtok_r(buffer, "\n", &saveptr);

    if (command == NULL) {
        const char *msg = "Error: No command received\n";
        send(client_socket, msg, strlen(msg), 0);
        return;
    }

    if (strcmp(command, "CREATE") == 0) {
        char *folderpath = strtok_r(NULL, "\n", &saveptr);
        char *f_path = strtok_r(NULL, "\n", &saveptr);

        if (folderpath && f_path) {
            char full_path[MAX_PATH_LENGTH];
            snprintf(full_path, sizeof(full_path), "%s/%s", folderpath, f_path);

            int ss_id = search_path(full_path, root);
            if (ss_id != -1) {
                const char *msg = "Error: File or folder already exists\n";
                send(client_socket, msg, strlen(msg), 0);
            } else {
                // For simplicity, select the first storage server
                int ss_id = 0;

                // Insert path into trie
                int chosen_servers[3];
                choose_least_full_servers(chosen_servers);
                insert_path(full_path, chosen_servers, root);


                // Update cache
                FileEntry *new_entry = malloc(sizeof(FileEntry));
                strcpy(new_entry->filename, full_path);
                for(int i = 0; i < 3; i++){
                    new_entry->ss_ids[i] = chosen_servers[i];
                }

                time_t now = time(NULL);
                struct tm *tm_info = gmtime(&now);  // Use localtime(&now) for local time

                strftime(new_entry->last_modified, sizeof(new_entry->last_modified), "%Y-%m-%dT%H:%M:%SZ", tm_info);

                cache_put(full_path, new_entry, cache);

                // TODO: Send CREATE command to storage server

                const char *msg = "Success: File or folder created\n";
                send(client_socket, msg, strlen(msg), 0);
            }
        } else {
            const char *msg = "Error: Invalid parameters for CREATE\n";
            send(client_socket, msg, strlen(msg), 0);
        }
    } else if (strcmp(command, "DELETE") == 0) {
        char *folderpath = strtok_r(NULL, "\n", &saveptr);
        char *f_path = strtok_r(NULL, "\n", &saveptr);

        if (folderpath && f_path) {
            char full_path[MAX_PATH_LENGTH];
            snprintf(full_path, sizeof(full_path), "%s/%s", folderpath, f_path);

            int ss_id = search_path(full_path, root);
            if (ss_id == -1) {
                const char *msg = "Error: File or folder not found\n";
                send(client_socket, msg, strlen(msg), 0);
            } else {
                // Remove path from trie and cache
                remove_path(full_path,root);
                cache_remove(full_path, cache);

                // TODO: Send DELETE command to storage server

                const char *msg = "Success: File or folder deleted\n";
                send(client_socket, msg, strlen(msg), 0);
            }
        } else {
            const char *msg = "Error: Invalid parameters for DELETE\n";
            send(client_socket, msg, strlen(msg), 0);
        }
    } else if (strcmp(command, "COPY") == 0) {
        char *sourcefilepath = strtok_r(NULL, "\n", &saveptr);
        char *destfolderpath = strtok_r(NULL, "\n", &saveptr);

        if (sourcefilepath && destfolderpath) {
            // TODO: Implement COPY logic

            const char *msg = "Success: File copied\n";
            send(client_socket, msg, strlen(msg), 0);
        } else {
            const char *msg = "Error: Invalid parameters for COPY\n";
            send(client_socket, msg, strlen(msg), 0);
        }
    } else if (strcmp(command, "INFO") == 0) {
        char *filepath = strtok_r(NULL, "\n", &saveptr);

        if (filepath) {
            int ss_id = search_path(filepath, root);
            if (ss_id != -1) {
                // TODO: Retrieve actual file info from storage server

                const char *file_info = "File size: 1024 bytes\nLast modified: 2023-11-01\n";
                send(client_socket, file_info, strlen(file_info), 0);

                const char *msg = "Success: File info retrieved\n";
                send(client_socket, msg, strlen(msg), 0);
            } else {
                const char *msg = "Error: File not found\n";
                send(client_socket, msg, strlen(msg), 0);
            }
        } else {
            const char *msg = "Error: Invalid parameters for INFO\n";
            send(client_socket, msg, strlen(msg), 0);
        }
    } else if (strcmp(command, "LIST") == 0) {
        char *folderpath = strtok_r(NULL, "\n", &saveptr);

        if (folderpath) {
            // TODO: Retrieve actual list from storage server

            const char *list = "file1.txt\nfile2.txt\nfolder1\n";
            send(client_socket, list, strlen(list), 0);

            const char *msg = "Success: List retrieved\n";
            send(client_socket, msg, strlen(msg), 0);
        } else {
            const char *msg = "Error: Invalid parameters for LIST\n";
            send(client_socket, msg, strlen(msg), 0);
        }
    } else if (strcmp(command, "READ") == 0 || strcmp(command, "STREAM") == 0 || strcmp(command, "WRITE") == 0) {
        char *filepath = strtok_r(NULL, "\n", &saveptr);
        if (filepath) {
            // Check in cache
            FileEntry *entry = cache_get(filepath, cache);
            if (entry != NULL) {
                int ss_id = entry->ss_ids[0];
                char response[256];
                snprintf(response, sizeof(response), "%s\n%d\n",
                         storage_servers[ss_id].ip_address,
                         storage_servers[ss_id].port);
                if(strcmp(command, "WRITE")==0){
                    time_t now = time(NULL);
                    struct tm *tm_info = gmtime(&now);  // Use localtime(&now) for local time

                    strftime(entry->last_modified, sizeof(entry->last_modified), "%Y-%m-%dT%H:%M:%SZ", tm_info);
                }
                send(client_socket, response, strlen(response), 0);
            } else {
                int ss_id = search_path(filepath, root);
                if (ss_id != -1) {
                    // Update cache
                    FileEntry *new_entry = malloc(sizeof(FileEntry));
                    strcpy(new_entry->filename, filepath);
                    new_entry->ss_ids[0] = ss_id;
                    cache_put(filepath, new_entry, cache);

                    // Send storage server info
                    char response[256];
                    snprintf(response, sizeof(response), "%s\n%d\n",
                             storage_servers[ss_id].ip_address,
                             storage_servers[ss_id].port);
                    if(strcmp(command, "WRITE")==0){
                        time_t now = time(NULL);
                        struct tm *tm_info = gmtime(&now);  // Use localtime(&now) for local time

                        strftime(entry->last_modified, sizeof(entry->last_modified), "%Y-%m-%dT%H:%M:%SZ", tm_info);
                    }
                    send(client_socket, response, strlen(response), 0);
                } else {
                    const char *msg = "Error: File not found\n";
                    send(client_socket, msg, strlen(msg), 0);
                }
            }
        } else {
            const char *msg = "Error: Invalid parameters\n";
            send(client_socket, msg, strlen(msg), 0);
        }
    } else {
        const char *msg = "Error: Invalid command\n";
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
    load_trie("trie_data.bin",root);

    load_cache("cache_data.bin",cache);

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
