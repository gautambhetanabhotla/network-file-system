#include "main.h"

//f Global port
TrieNode *root;
StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
int storage_server_count = 0;
struct lru_cache *cache; // Cache pointer
sem_t storage_server_sem;                 // Semaphore to track storage servers
pthread_mutex_t storage_server_mutex;     // Mutex to protect storage_server_count
int round_robin_counter = 0;

void handle_create_request(int client_socket, int client_req_id, char *content, long content_length);


// Helper function to write n bytes to a socket
ssize_t write_n_bytes(int socket_fd, const void *buffer, size_t n)
{
    size_t total_written = 0;
    const char *buf = (const char *)buffer;
    while (total_written < n)
    {
        ssize_t bytes_written = send(socket_fd, buf + total_written, n - total_written, 0);
        if (bytes_written <= 0)
        {
            return -1; // Error
        }
        total_written += bytes_written;
    }
    return total_written;
}

// Signal Handler to save data on exit

// Function to send an error response to the client
void send_error_response(int client_socket, int req_id, const char *error_msg)
{
    char header[31];
    char req_id_str[10];
    snprintf(req_id_str, sizeof(req_id_str), "%09d", req_id);

    char content_length_str[21];
    size_t content_length = strlen(error_msg);
    snprintf(content_length_str, sizeof(content_length_str), "%020ld", content_length);

    header[0] = '1'; // Assuming '1' indicates an error acknowledgment
    strncpy(&header[1], req_id_str, 9);
    strncpy(&header[10], content_length_str, 20);
    header[30] = '\0';

    // Send header and content
    write_n_bytes(client_socket, header, 30);
    write_n_bytes(client_socket, error_msg, content_length);
}

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
    char *path = strtok(paths, "\n");
    while (path != NULL)
    {
        // Insert the path into the trie
        insert_path(path, &ss_id, 1, root);

        // Move to the next path
        path = strtok(NULL, "\n");
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
        data_buffer[content_len] = '\0'; // Null-terminate the data

        // Process the DATA
        // First 5 bytes are the port number
        char port_str[6];
        memcpy(port_str, data_buffer, 5);
        port_str[5] = '\0'; // Null-terminate the port string
        int port = atoi(port_str);

        // Remaining data contains strings (paths)
        char* remaining_data = data_buffer + 5;
        // size_t remaining_length = content_len - 5;

        // Accumulate the required strings (skip every second string)
        char accumulated_paths[16384]; // Adjust size as needed
        size_t accumulated_length = 0;
        int token_counter = 0;

        char* token = strtok(remaining_data, " \n");
        while (token != NULL)
        {
            if (token_counter % 2 == 0)
            {
                size_t token_length = strlen(token);
                // Ensure we don't exceed the buffer size
                if (accumulated_length + token_length + 1 < sizeof(accumulated_paths))
                {
                    memcpy(accumulated_paths + accumulated_length, token, token_length);
                    accumulated_length += token_length;
                    accumulated_paths[accumulated_length] = '\n'; // Separate paths with a newline
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
            token_counter++;
            token = strtok(NULL, " \n");
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
            char request_type;
            bytes_received = read_n_bytes(client_socket, &request_type, 1);
            if (bytes_received <= 0)
            {
                fprintf(stderr, "Failed to read REQUEST_TYPE or connection closed\n");
                close(client_socket);
                break;
            }
            fprintf(stderr, "recieved message %c\n", request_type);

            // Handle REQUEST_TYPE
            if (request_type == ':') // Storage server connection
            {
                fprintf(stderr, "Received new message from storage server\n");

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
                content_len = atoi(content_length);

                // Allocate buffer for data
                data_buffer = malloc(content_len + 1);
                if (data_buffer == NULL)
                {
                    fprintf(stderr, "Failed to allocate memory for data buffer\n");
                    close(client_socket);
                    return NULL;
                }

                // Read DATA (CONTENT_LENGTH bytes) in a loop
                total_bytes_read = 0;
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
                data_buffer[content_len] = '\0'; // Null-terminate the data

                // Process the DATA
                // First 5 bytes are the port number
                memcpy(port_str, data_buffer, 5);
                port_str[5] = '\0'; // Null-terminate the port string
                port = atoi(port_str);

                // Remaining data contains strings (paths)
                remaining_data = data_buffer + 5;
                // remaining_length = content_len - 5;

                // Accumulate the required strings (skip every second string)
                accumulated_length = 0;
                token_counter = 0;

                token = strtok(remaining_data, " \n");
                while (token != NULL)
                {
                    if (token_counter % 2 == 0)
                    {
                        size_t token_length = strlen(token);
                        // Ensure we don't exceed the buffer size
                        if (accumulated_length + token_length + 1 < sizeof(accumulated_paths))
                        {
                            memcpy(accumulated_paths + accumulated_length, token, token_length);
                            accumulated_length += token_length;
                            accumulated_paths[accumulated_length] = '\n'; // Separate paths with a newline
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
                    token_counter++;
                    token = strtok(NULL, " \n");
                }
                accumulated_paths[accumulated_length] = '\0'; // Null-terminate the accumulated paths

                // Call handle_storage_server with the port number and accumulated paths
                handle_storage_server(client_socket, id, port, accumulated_paths);
                fprintf(stderr, "Finished handling storage server\n");

                free(data_buffer);
            }
            else
            {
                fprintf(stderr, "Unknown REQUEST_TYPE received: %c\n", request_type);
                close(client_socket);
                return NULL;
            }
        }
        fprintf(stderr, "done bye\n");
    }
    else
    {
        // Handle client connections
        //fprintf(stderr, "Received request from client\n");
        fprintf(stderr, "request type: %c\n", request_type);
        handle_client(client_socket, request_type);
        close(client_socket);
    }
    return NULL;
}

void handle_client(int client_socket, char initial_request_type)
{
    char header[30]; // 30 bytes
    ssize_t bytes_received;

    // We already received the first byte of the header as initial_request_type
    header[0] = initial_request_type;
    fprintf(stderr, "initial_request_type: %c\n", header[0]);
    // Read the remaining 29 bytes of the header
    bytes_received = read_n_bytes(client_socket, &header[1], 29);
    if (bytes_received != 29)
    {
        fprintf(stderr, "Failed to read header from client\n");
        close(client_socket);
        return;
    }
    //fprintf(stderr, "blalalal\n");
    fprintf(stderr, "header: %s\n", header);

    
    char request_type = header[0];

    // Parse header fields
    char id_str[10];
    char content_length_str[21];

    strncpy(id_str, &header[1], 9);
    fprintf(stderr, "id_str: %s\n", id_str);
    id_str[9] = '\0';

    strncpy(content_length_str, &header[10], 20);
    content_length_str[20] = '\0';
    fprintf(stderr, "content_length_str: %s\n", content_length_str);

    int client_req_id = atoi(id_str);
    long content_length = atol(content_length_str);

    // Read content from client
    char *content = malloc(content_length + 1);
    if (!content)
    {
        fprintf(stderr, "Failed to allocate memory for content\n");
        close(client_socket);
        return;
    }

    bytes_received = read_n_bytes(client_socket, content, content_length);
    if (bytes_received != content_length)
    {
        fprintf(stderr, "Failed to read content from client\n");
        free(content);
        close(client_socket);
        return;
    }
    content[content_length] = '\0';

    // Handle the request based on request_type
    if (request_type == '6') // '6' for CREATE
    {
        fprintf(stderr, "Received CREATE request from client\n");
        handle_create_request(client_socket, client_req_id, content, content_length);
    }
    else
    {
        fprintf(stderr, "Invalid request type received: %c\n", request_type);
        send_error_response(client_socket, client_req_id, "Error: Invalid request type\n");
    }

    free(content);
}

void handle_create_request(int client_socket, int client_req_id, char *content, long content_length){
    fprintf(stderr, "Handling create request %d %s %ld\n", client_req_id, content, content_length);
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
    // int addrlen = sizeof(address);

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

