#include "main.h"

//f Global port
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
    storage_servers[id].is_active = 1;
    printf("Registered Storage Server %d: %s:%d\n", id, ip, port);
    return id;
}

void handle_storage_server(int client_socket, char *id, int port, char *paths)
{
    fprintf(stderr, "Handling storage server with ID: %s\n", id);

    // Get storage server IP
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    getpeername(client_socket, (struct sockaddr *)&addr, &addr_size);
    char *ip = inet_ntoa(addr.sin_addr);

    // Register storage server
    pthread_mutex_lock(&storage_server_mutex);
    int ss_id = register_storage_server(ip, port);
    pthread_mutex_unlock(&storage_server_mutex);

    // Process the accumulated paths and insert them into the trie
    char* saveptr1;
    char* saveptr2;
    char *line = strtok_r(paths, "\n", &saveptr1);
    while (line != NULL)
    {
        // Split the line into path and timestamp
        char *path = strtok_r(line, " ", &saveptr2);
        char *timestamp = strtok_r(NULL, " ", &saveptr2);

        if (path && timestamp)
        {
            // Insert the path into the trie
            FileEntry* file = insert_path(path, &ss_id, 1, root);

            // Set the timestamp for the file entry
            // Assuming you have a function to set the timestamp
            set_file_entry_timestamp(file, timestamp);
            fprintf(stderr, "Inserted path: %s with timestamp %s\n", path, file->last_modified);
        }

        // Move to the next line
        line = strtok_r(NULL, "\n", &saveptr1);
    }

    // If this is the first storage server, post the semaphore
    pthread_mutex_lock(&storage_server_mutex);
    if (storage_server_count == 1)
    {
        printf("At least one storage server is connected\n");
        sem_post(&storage_server_sem);
    }
    pthread_mutex_unlock(&storage_server_mutex);
}


void *handle_connection(void *arg)
{
    int client_socket = *(int *)arg;
    free(arg);

    char request_type;
    ssize_t bytes_received;

    // Read REQUEST_TYPE (1 byte)
    bytes_received = read_n_bytes(client_socket, &request_type, 1);
    if (bytes_received != 1)
    {
        fprintf(stderr, "Failed to read REQUEST_TYPE\n");
        close(client_socket);
        return NULL;
    }

    // Debug: Print the request_type
    fprintf(stderr, "Received REQUEST_TYPE: %c (ASCII: %d)\n", request_type, request_type);

    // Handle REQUEST_TYPE
    if (request_type == ':') // Storage server connection
    {
        fprintf(stderr, "Received HELLO from storage server\n");

        char id[10];
        char content_length[21];

        // Read ID (9 bytes)
        bytes_received = read_n_bytes(client_socket, id, 9);
        if (bytes_received != 9)
        {
            fprintf(stderr, "Failed to read ID\n");
            close(client_socket);
            return NULL;
        }
        id[9] = '\0';

        // Read CONTENT_LENGTH (20 bytes)
        bytes_received = read_n_bytes(client_socket, content_length, 20);
        if (bytes_received != 20)
        {
            fprintf(stderr, "Failed to read content length\n");
            close(client_socket);
            return NULL;
        }
        content_length[20] = '\0';
        int content_len = atoi(content_length);

        // Allocate buffer for data
        char* data_buffer = malloc(content_len + 1);
        if (data_buffer == NULL)
        {
            fprintf(stderr, "Failed to allocate memory for data buffer\n");
            close(client_socket);
            return NULL;
        }

        // Read DATA (CONTENT_LENGTH bytes) in a loop
        size_t total_bytes_read = 0;
        while (total_bytes_read < content_len)
        {
            bytes_received = recv(client_socket, data_buffer + total_bytes_read, content_len - total_bytes_read, 0);
            if (bytes_received <= 0)
            {
                fprintf(stderr, "Failed to read data\n");
                free(data_buffer);
                close(client_socket);
                return NULL;
            }
            total_bytes_read += bytes_received;
        }
        if(data_buffer[content_len-1]=='\0'){
            fprintf(stderr,"null character because");
        }
        data_buffer[content_len-1] = '\0'; // Null-terminate the data
        data_buffer[content_len] = '\0'; // Null-terminate the data
        fprintf(stderr, "Received data: %s\n", data_buffer);

        // Process the DATA
        // First 5 bytes are the port number
        char port_str[6];
        memcpy(port_str, data_buffer, 5);
        port_str[5] = '\0'; // Null-terminate the port string
        int port = atoi(port_str);

        // Remaining data contains strings (paths)
        char* remaining_data = data_buffer + 5;
        size_t remaining_length = content_len - 5;
        fprintf(stderr, "Remaining data: ");
        for(int i=0;i<remaining_length;i++){
            if(remaining_data[i] =='\0'){
                //fprintf(stderr,"null character");
                remaining_data[i] = ' ';
            }
            fprintf(stderr,"%c",remaining_data[i]);
        }
        fprintf(stderr,"\n");

        // Accumulate the required strings (skip every second string)
        char accumulated_paths[16384]; // Adjust size as needed
        size_t accumulated_length = 0;

        char* line;
        char* saveptr1;
        line = strtok_r(remaining_data, "\n", &saveptr1);
        fprintf(stderr, "saveptr: ");
        for(int i=0; i< (remaining_length - strlen(line));i++)
        {
            fprintf(stderr,"%c",saveptr1[i]);
        }
        fprintf(stderr,"\n");
        while (line != NULL)
        {
            // Skip blank lines
            if (strlen(line) == 0)
            {
                fprintf(stderr, "Skipping blank line\n");
                line = strtok_r(NULL, "\n", &saveptr1);
                continue;
            }


            // Split the line into tokens
            char* path;
            char* skip;
            char* timestamp;
            char* saveptr2;

            path = strtok_r(line, " ", &saveptr2);
            skip = strtok_r(NULL, " ", &saveptr2);
            timestamp = strtok_r(NULL, " ", &saveptr2);

            if (path && timestamp)
            {
                size_t path_length = strlen(path);
                size_t timestamp_length = strlen(timestamp);

                // Ensure we don't exceed the buffer size
                if (accumulated_length + path_length + timestamp_length + 2 < sizeof(accumulated_paths))
                {
                    memcpy(accumulated_paths + accumulated_length, path, path_length);
                    accumulated_length += path_length;
                    accumulated_paths[accumulated_length] = ' '; // Separate path and timestamp with a space
                    accumulated_length++;
                    memcpy(accumulated_paths + accumulated_length, timestamp, timestamp_length);
                    accumulated_length += timestamp_length;
                    accumulated_paths[accumulated_length] = '\n'; // Separate entries with a newline
                    accumulated_length++;
                }
                else
                {
                    fprintf(stderr, "Accumulated paths buffer overflow\n");
                    free(data_buffer);
                    close(client_socket);
                    return NULL;
                }
            }

            line = strtok_r(NULL, "\n", &saveptr1);
        }
        accumulated_paths[accumulated_length] = '\0'; // Null-terminate the accumulated paths

        // Call handle_storage_server with the port number and accumulated paths
        handle_storage_server(client_socket, id, port, accumulated_paths);
        fprintf(stderr, "Finished handling storage server\n");

        free(data_buffer);

        // Continue to listen to the storage server if needed
        while (1)
        {
            fprintf(stderr, "reading\n");
            bytes_received = read_n_bytes(client_socket, &request_type, 1);
            if (bytes_received != 1)
            {
                fprintf(stderr, "Failed to read REQUEST_TYPE\n");
                close(client_socket);
                return NULL;
            }
        }
    }
    else
    {
        // Handle client connections
        handle_client(client_socket, &request_type);
        close(client_socket);
    }

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
        fprintf(stderr, "new connection accepted");
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

