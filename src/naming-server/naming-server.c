#include "main.h"

// Global port
TrieNode *root;
StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
int storage_server_count = 0;
struct lru_cache *cache; // Cache pointer
sem_t storage_server_sem;                 // Semaphore to track storage servers
pthread_mutex_t storage_server_mutex;     // Mutex to protect storage_server_count
int round_robin_counter = 0;

// Signal Handler to save data on exit

ssize_t read_n_bytes(int socket_fd, void *buffer, size_t n)
{
    size_t total_read = 0;
    char *buf = (char *)buffer;
    while (total_read < n)
    {
        ssize_t bytes_read = recv(socket_fd, buf + total_read, n - total_read, 0);
        if (bytes_read < 0)
        {
            perror("recv");
            return -1; // Error occurred
        }
        else if (bytes_read == 0)
        {
            // Connection closed
            fprintf(stderr, "Connection closed by peer\n");
            return -1;
        }
        total_read += bytes_read;
    }
    return total_read;
}

void choose_least_full_servers(int *chosen_servers, int *num_chosen)
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
            *num_chosen = (*num_chosen < 3) ? *num_chosen + 1 : 3;
        }
        else if (file_count < min_file_counts[1])
        {
            min_file_counts[2] = min_file_counts[1];
            chosen_servers[2] = chosen_servers[1];
            min_file_counts[1] = file_count;
            chosen_servers[1] = i;
            *num_chosen = (*num_chosen < 3) ? *num_chosen + 1 : 3;
        }
        else if (file_count < min_file_counts[2])
        {
            min_file_counts[2] = file_count;
            chosen_servers[2] = i;
            *num_chosen = (*num_chosen < 3) ? *num_chosen + 1 : 3;
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
        printf("Received path: %s\n", token);

        int chosen_servers[3];
        int num_chosen;
        choose_least_full_servers(chosen_servers, &num_chosen);
        insert_path(token, chosen_servers, num_chosen, root);
    }
}


void *handle_connection(void *arg)
{
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[1024];
    char accumulated_buffer[16384]; // Larger buffer to accumulate data
    int accumulated_length = 0;
    ssize_t bytes_received;

    // Receive 13 bytes to check for the "STORAGESERVER" flag
    bytes_received = read_n_bytes(client_socket, buffer, 13);
    if (bytes_received != 13)
    {
        fprintf(stderr, "Failed to read STORAGESERVER flag\n");
        close(client_socket);
        return NULL;
    }
    buffer[13] = '\0';
    printf("Received: %s\n", buffer);

    // Check if the message is from a storage server
    if (strncmp(buffer, "STORAGESERVER", 13) == 0)
    {
        // Add "STORAGESERVER" to the accumulated buffer
        memcpy(accumulated_buffer + accumulated_length, buffer, 13);
        accumulated_length += 13;

        // Receive the port number (5 bytes)
        char port_str[6];
        bytes_received = read_n_bytes(client_socket, port_str, 5);
        if (bytes_received != 5)
        {
            fprintf(stderr, "Failed to read port number\n");
            close(client_socket);
            return NULL;
        }
        port_str[5] = '\0';
        printf("Port: %s\n", port_str);
        // Add the port number to the accumulated buffer
        memcpy(accumulated_buffer + accumulated_length, port_str, 5);
        accumulated_length += 5;

        // Receive the content length (20 bytes)
        char content_length_str[21];
        bytes_received = read_n_bytes(client_socket, content_length_str, 20);
        if (bytes_received != 20)
        {
            fprintf(stderr, "Failed to read content length\n");
            close(client_socket);
            return NULL;
        }
        content_length_str[20] = '\0';
        int content_length = atoi(content_length_str);
        printf("Content length: %d\n", content_length);

        // Add the content length to the accumulated buffer
        memcpy(accumulated_buffer + accumulated_length, content_length_str, 20);
        accumulated_length += 20;

        // Receive the specified amount of data based on the content length
        int total_bytes_read = 0;
        int token_counter = 0;
        while (total_bytes_read < content_length)
        {
            ssize_t chunk_size = (content_length - total_bytes_read) > (sizeof(buffer) - 1) ? (sizeof(buffer) - 1) : (content_length - total_bytes_read);
            bytes_received = recv(client_socket, buffer, chunk_size, 0);
            if (bytes_received < 0)
            {
                perror("recv");
                close(client_socket);
                return NULL;
            }
            else if (bytes_received == 0)
            {
                fprintf(stderr, "Connection closed by peer during data reception\n");
                close(client_socket);
                return NULL;
            }

            buffer[bytes_received] = '\0';
            fprintf(stderr, "Received: %s\n", buffer);
            // Split the buffer into tokens based on spaces and newlines
            char *token = strtok(buffer, " \n");
            while (token != NULL)
            {
                // Skip blank tokens
                if (strlen(token) == 0)
                {
                    token = strtok(NULL, " \n");
                    continue;
                }
                // Skip every second token
                if (token_counter % 2 == 0)
                {
                    // Accumulate data in the buffer
                    if (accumulated_length + strlen(token) < sizeof(accumulated_buffer) - 1)
                    {
                        memcpy(accumulated_buffer + accumulated_length, token, strlen(token));
                        accumulated_length += strlen(token);
                        accumulated_buffer[accumulated_length] = ' '; // Add space character
                        accumulated_length++;
                    }
                    else
                    {
                        fprintf(stderr, "Accumulated buffer overflow\n");
                        close(client_socket);
                        return NULL;
                    }
                }
                token_counter++;
                token = strtok(NULL, " \n");
            }

            total_bytes_read += bytes_received;
        }

        // Add null terminator at the end of the accumulated buffer
        if (accumulated_length < sizeof(accumulated_buffer))
        {
            accumulated_buffer[accumulated_length] = '\0';
        }
        else
        {
            accumulated_buffer[sizeof(accumulated_buffer) - 1] = '\0';
        }

        // Debug print of the accumulated buffer
        fprintf(stderr, "Accumulated buffer: %s\n", accumulated_buffer);

        // Call handle_storage_server with the accumulated data
        handle_storage_server(client_socket, accumulated_buffer);

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
        // Handle client connections...
        handle_client(client_socket, buffer);
    }

    close(client_socket);
    return NULL;
}

//Handle Client Requests
void handle_client(int client_socket, char *initial_buffer)
{
    char buffer[1024];
    strcpy(buffer, initial_buffer);  // Copy the initial buffer

    // Parse the command and arguments
    char *saveptr;
    char *command = strtok_r(buffer, "\n", &saveptr);

    if (command == NULL)
    {
        const char *msg = "Error: No command received\n";
        send(client_socket, msg, strlen(msg), 0);
        return;
    }

    if (strcmp(command, "CREATE") == 0)
    {
        char *folderpath = strtok_r(NULL, "\n", &saveptr);
        char *f_path = strtok_r(NULL, "\n", &saveptr);

        if (folderpath && f_path)
        {
            char full_path[MAX_PATH_LENGTH];
            snprintf(full_path, sizeof(full_path), "%s/%s", folderpath, f_path);

            int ss_id = search_path(full_path, root);
            if (ss_id != -1)
            {
                const char *msg = "Error: File or folder already exists\n";
                send(client_socket, msg, strlen(msg), 0);
            }
            else
            {
                // Choose the three least full storage servers
                int chosen_servers[3];
                int num_chosen;
                choose_least_full_servers(chosen_servers, &num_chosen);

                // Insert path into trie
                insert_path(full_path, chosen_servers, num_chosen, root);

                // Update cache
                FileEntry *new_entry = malloc(sizeof(FileEntry));
                strcpy(new_entry->filename, full_path);
                for (int i = 0; i < num_chosen; i++)
                {
                    new_entry->ss_ids[i] = chosen_servers[i];
                }
                cache_put(full_path, new_entry, cache);

                // TODO: Send CREATE command to storage servers

                const char *msg = "Success: File or folder created\n";
                send(client_socket, msg, strlen(msg), 0);
            }
        }
        else
        {
            const char *msg = "Error: Invalid parameters for CREATE\n";
            send(client_socket, msg, strlen(msg), 0);
        }
    }
    else if (strcmp(command, "READ") == 0 || strcmp(command, "STREAM") == 0 || strcmp(command, "WRITE") == 0)
    {
        char *filepath = strtok_r(NULL, "\n", &saveptr);
        if (filepath)
        {
            // Check in cache
            FileEntry *entry = cache_get(filepath, cache);
            if (entry != NULL)
            {
                // Use round-robin scheduling to select the storage server ID
                pthread_mutex_lock(&storage_server_mutex);
                int ss_id = entry->ss_ids[round_robin_counter % 3];
                round_robin_counter++;
                pthread_mutex_unlock(&storage_server_mutex);

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
            }
            else
            {
                int ss_id = search_path(filepath, root);
                if (ss_id != -1)
                {
                    // Update cache
                    FileEntry *new_entry = malloc(sizeof(FileEntry));
                    strcpy(new_entry->filename, filepath);
                    new_entry->ss_ids[0] = ss_id;
                    cache_put(filepath, new_entry, cache);

                    // Use round-robin scheduling to select the storage server ID
                    pthread_mutex_lock(&storage_server_mutex);
                    int selected_ss_id = new_entry->ss_ids[round_robin_counter % 3];
                    round_robin_counter++;
                    pthread_mutex_unlock(&storage_server_mutex);

                    // Send storage server info
                    char response[256];
                    snprintf(response, sizeof(response), "%s\n%d\n",
                             storage_servers[selected_ss_id].ip_address,
                             storage_servers[selected_ss_id].port);
                    if (strcmp(command, "WRITE") == 0)
                    {
                        time_t now = time(NULL);
                        struct tm *tm_info = gmtime(&now);  // Use localtime(&now) for local time

                        strftime(new_entry->last_modified, sizeof(new_entry->last_modified), "%Y-%m-%dT%H:%M:%SZ", tm_info);
                    }
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
            const char *msg = "Error: Invalid parameters\n";
            send(client_socket, msg, strlen(msg), 0);
        }
    }
    else
    {
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

