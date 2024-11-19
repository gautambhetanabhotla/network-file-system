#include "main.h"

// f Global port
TrieNode *root;
StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
int storage_server_count = 0;
struct lru_cache *cache;              // Cache pointer
sem_t storage_server_sem;             // Semaphore to track storage servers
pthread_mutex_t storage_server_mutex; // Mutex to protect storage_server_count
int round_robin_counter = 0;
pthread_mutex_t global_req_id_mutex; // Mutex to protect global request ID
int global_req_id = 1;
pthread_mutex_t trie_mutex; // Mutex to protect trie operations

void handle_create_request(int client_socket, int client_req_id, char *content, long content_length);
int delete_file(const char *path, FileEntry *entry);
int send_success(int client_socket, int client_req_id, char *message);
void list_paths(TrieNode *node, const char *base_path, char **output, size_t *output_length);
int connect_to_storage_server(const char *ip_address, int port);

TrieNode *search_trie_node(const char *path, TrieNode *root)
{
    TrieNode *current_node = root;
    const char *p = path;

    while (*p)
    {
        // Skip multiple slashes
        while (*p == '/')
            p++;

        if (*p == '\0')
            break;

        // Extract the next component of the path
        char name[MAX_FILENAME_LENGTH];
        int i = 0;
        while (*p != '/' && *p != '\0' && i < MAX_FILENAME_LENGTH - 1)
        {
            name[i++] = *p;
            p++;
        }
        name[i] = '\0';

        // Traverse to the next node
        if (current_node->children[(unsigned char)name[0]])
        {
            current_node = current_node->children[(unsigned char)name[0]];
            // Continue matching the rest of the name
            for (int j = 1; name[j] != '\0'; j++)
            {
                if (current_node->children[(unsigned char)name[j]])
                {
                    current_node = current_node->children[(unsigned char)name[j]];
                }
                else
                {
                    return NULL;
                }
            }

            // Check if the node represents the complete name
            if (current_node->file_entry == NULL || strcmp(current_node->file_entry->filename, name) != 0)
            {
                return NULL;
            }
        }
        else
        {
            return NULL;
        }
    }

    return current_node;
}

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

void send_port_ip(int client_socket, int port, char *ip)
{
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
    chosen_servers[0] = -1;
    chosen_servers[1] = -1;
    chosen_servers[2] = -1;
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

void handle_rws_request(int client_socket, int client_req_id, char *content, long content_length, char request_type)
{
    char *path_buffer = malloc(content_length + 1);
    if (path_buffer == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for data buffer\n");
        // close(client_socket);
        return;
    }
    int total_read = 0;
    fprintf(stderr, "content_length: %ld\n", content_length);
    fprintf(stderr, "content: %s\n", content);

    FileEntry *file = search_path(content, root);
    if (file == NULL)
    {
        fprintf(stderr, "path not found\n");
        send_error_response(client_socket, client_req_id, "path not found\n");
    }
    else
    {
        // Get storage server information
        int id = file->ss_ids[0];
        StorageServerInfo ss_info = storage_servers[id];
        // Prepare response content with IP and Port
        char response_content[256];
        snprintf(response_content, sizeof(response_content), "%s\n%d\n", ss_info.ip_address, ss_info.port);
        size_t response_content_length = strlen(response_content);

        // Prepare header
        char header[31]; // 30 bytes + null terminator
        char req_id_str[10];
        char content_length_str[21];

        snprintf(req_id_str, sizeof(req_id_str), "%09d", client_req_id);
        snprintf(content_length_str, sizeof(content_length_str), "%020zu", response_content_length);

        header[0] = '0'; // Acknowledgment byte indicating success
        strncpy(&header[1], req_id_str, 9);
        strncpy(&header[10], content_length_str, 20);
        header[30] = '\0';

        // Send header and content to client
        if (write_n_bytes(client_socket, header, 30) != 30 ||
            write_n_bytes(client_socket, response_content, response_content_length) != (ssize_t)response_content_length)
        {
            fprintf(stderr, "Failed to send response to client\n");
            // close(client_socket);
            free(path_buffer);
            return;
        }
        fprintf(stderr, "Sent storage server info to client: IP=%s, Port=%d\n", ss_info.ip_address, ss_info.port);
    }

    free(path_buffer);
    fprintf(stderr, "Handled rws request %d %s %ld %c\n", client_req_id, content, content_length, request_type);
}

int delete_directory(const char *path)
{
    // Use existing collect_paths function to gather all paths under the directory
    char *response_content = NULL;
    size_t response_content_length = 0;

    pthread_mutex_lock(&trie_mutex);
    FileEntry *dir_node = search_path(path, root);
    pthread_mutex_unlock(&trie_mutex);

    if (dir_node == NULL)
    {
        fprintf(stderr, "Directory does not exist: %s\n", path);
        return -1;
    }

    // Collect all file and directory paths under the given directory
    list_paths(dir_node, path, &response_content, &response_content_length);

    if (response_content == NULL || response_content_length == 0)
    {
        fprintf(stderr, "No files or directories found under %s\n", path);
        return -1;
    }

    // Split the collected paths into an array
    char **paths = NULL;
    size_t num_paths = 0;

    char *saveptr;
    char *line = strtok_r(response_content, "\n", &saveptr);
    while (line != NULL)
    {
        paths = realloc(paths, (num_paths + 1) * sizeof(char *));
        paths[num_paths] = strdup(line);
        num_paths++;
        line = strtok_r(NULL, "\n", &saveptr);
    }

    int result = 0;

    // Delete each file or directory
    for (size_t i = 0; i < num_paths; i++)
    {
        pthread_mutex_lock(&trie_mutex);
        FileEntry *entry = search_path(paths[i], root);
        pthread_mutex_unlock(&trie_mutex);

        if (entry == NULL)
            continue;

        if (entry->is_folder == 0)
        {
            // It's a file
            if (delete_file(paths[i], entry) < 0)
            {
                result = -1;
            }
        }
        else
        {
            // It's a directory, remove from trie
            pthread_mutex_lock(&trie_mutex);
            remove_path(paths[i], root);
            pthread_mutex_unlock(&trie_mutex);
        }

        free(paths[i]);
    }

    free(paths);
    free(response_content);

    // Remove the directory itself from the trie
    pthread_mutex_lock(&trie_mutex);
    remove_path(path, root);
    pthread_mutex_unlock(&trie_mutex);

    return result;
}

int delete_file(const char *path, FileEntry *entry)
{
    int success_count = 0;

    // Send delete request to all storage servers that have the file
    for (int i = 0; i < 3; i++)
    {
        int ss_id = entry->ss_ids[i];
        if (ss_id == -1)
            continue;

        StorageServerInfo ss_info = storage_servers[ss_id];

        int ss_socket = connect_to_storage_server(ss_info.ip_address, ss_info.port);
        if (ss_socket < 0)
        {
            fprintf(stderr, "Failed to connect to storage server %d\n", ss_id);
            continue;
        }

        // Prepare header
        char header[31] = {0};
        char req_id_str[10];
        char content_length_str[21];
        pthread_mutex_lock(&global_req_id_mutex);
        int storage_req_id = global_req_id++;
        pthread_mutex_unlock(&global_req_id_mutex);
        snprintf(req_id_str, sizeof(req_id_str), "%09d", storage_req_id);
        snprintf(content_length_str, sizeof(content_length_str), "%020ld", strlen(path) + 1);

        header[0] = '7'; // '7' for DELETE operation
        strncpy(&header[1], req_id_str, 9);
        strncpy(&header[10], content_length_str, 20);

        // Send header and path
        if (write_n_bytes(ss_socket, header, 30) != 30 ||
            write_n_bytes(ss_socket, path, strlen(path) + 1) != (ssize_t)(strlen(path) + 1))
        {
            fprintf(stderr, "Failed to send delete request to storage server %d\n", ss_id);
            // close(ss_socket);
            continue;
        }

        // Read response
        char response_header[31];
        if (read_n_bytes(ss_socket, response_header, 30) != 30)
        {
            fprintf(stderr, "Failed to read response from storage server %d\n", ss_id);
            // close(ss_socket);
            continue;
        }

        if (response_header[0] == '0') // '0' indicates success
        {
            success_count++;
        }
        else
        {
            fprintf(stderr, "Error response from storage server %d\n", ss_id);
        }

        // close(ss_socket);
    }

    if (success_count == 0)
    {
        fprintf(stderr, "Failed to delete file from all storage servers\n");
        return -1;
    }

    // Remove the file entry from the trie
    pthread_mutex_lock(&trie_mutex);
    remove_path(path, root);
    pthread_mutex_unlock(&trie_mutex);

    return 0;
}

void handle_create_request(int client_socket, int client_req_id, char *content, long content_length)
{
    // Parse content into folderpath and name
    char *folderpath = (char *)malloc(content_length + 1);
    char *name = (char *)malloc(content_length + 1);
    fprintf(stderr, "content: %s\n", content);

    char *saveptr;
    char *exist_folder = strtok_r(content, "\n", &saveptr);
    fprintf(stderr, "exist_folder: %s\n", exist_folder);

    FileEntry *file = search_path(exist_folder, root);
    if (file == NULL)
    {
        fprintf(stderr, "Folder does not exist\n");
        send_error_response(client_socket, client_req_id, "Error: Folder does not exist\n");
        return;
    }
    else if (file->is_folder == 0)
    {
        fprintf(stderr, "Path is not a folder\n");
        send_error_response(client_socket, client_req_id, "Error: Path is not a folder\n");
        return;
    }
    else
    {
        fprintf(stderr, "Folder exists\n");
        char *to_create = strtok_r(NULL, "\n", &saveptr);
        fprintf(stderr, "to_create: %s\n", to_create);
        char file_path[4096];
        snprintf(file_path, sizeof(file_path), "%s%s", exist_folder, to_create);
        file_path[strlen(file_path)] = '\0';
        if (to_create[strlen(to_create) - 1] == '/')
        {
            FileEntry *file = insert_path(file_path, NULL, 0, root);
            if (file != NULL)
            {
                file->is_folder = 1;
            }
        }
        else
        {

            // set timestamp
            time_t t = time(NULL);
            struct tm tm = *localtime(&t);
            char timestamp[20];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm);
            fprintf(stderr, "timestamp: %s\n", timestamp);

            char file_path[4096];

            size_t folderpath_len = strlen(exist_folder);
            size_t name_len = strlen(to_create);
            if (folderpath_len + name_len + 1 >= sizeof(file_path))
            {
                fprintf(stderr, "Path too long\n");
                send_error_response(client_socket, client_req_id, "Error: Path too long\n");
                return;
            }
            snprintf(file_path, sizeof(file_path), "%s%s", exist_folder, to_create);
            file_path[strlen(file_path)] = '\0';
            fprintf(stderr, "file_path: %s\n", file_path);
            size_t content_length = strlen(file_path) + 1 + strlen(timestamp);

            char *content = malloc(content_length + 1);
            if (content == NULL)
            {
                fprintf(stderr, "Failed to allocate memory for content\n");
                send_error_response(client_socket, client_req_id, "Error: Memory allocation failed\n");
                return;
            }

            strncpy(content, file_path, strlen(file_path));
            content[strlen(file_path)] = '\n';
            fprintf(stderr, "content: %s\n", content);
            strncpy(content + strlen(content), timestamp, strlen(timestamp));
            content[content_length] = '\n';
            fprintf(stderr, "content: %s\n", content);

            char header[31]; // 30 bytes + null terminator
            char req_id_str[10];
            char content_length_str[21];
            memset(header, 0, sizeof(header));
            memset(req_id_str, 0, sizeof(req_id_str));
            memset(content_length_str, 0, sizeof(content_length_str));
            // add content length to content length string as character
            snprintf(content_length_str, sizeof(content_length_str), "%ld", content_length);
            fprintf(stderr, "content_length_str: %s\n", content_length_str);

            char operation_type = '6'; // '6' for CREATE
            // send 30 bytes header: 1 byte operation type, 9 bytes request id, 20 bytes content length
            // generate new content length that is length of filepath + new line + timestamp size  using strnc

            // set global request id to req_id_str use strncpy, convert global_req_id to string
            snprintf(req_id_str, sizeof(req_id_str), "%09d", global_req_id);
            fprintf(stderr, "req_id_str: %s\n", req_id_str);

            header[0] = operation_type;
            strncpy(&header[1], req_id_str, strlen(req_id_str));
            //
            strncpy(&header[10], content_length_str, strlen(content_length_str));
            header[30] = '\n';
            // send request to the 3 least filled storage servers of the file entry
            int chosen_servers[3];
            int num_chosen = 0;
            choose_least_full_servers(chosen_servers, &num_chosen);

            if (num_chosen == 0)
            {
                send_error_response(client_socket, client_req_id, "No storage servers available\n");
                return;
            }

            for (int i = 0; i < num_chosen; i++) // what if there are less than 3 ???
            {
                // send request to storage server
                // send header along file path to the storage server
                int ssid = chosen_servers[i];
                StorageServerInfo ss_info = storage_servers[ssid];
                // connect to storage server using IP and Port in the ssinfo using connect and sockaddr
                struct sockaddr_in storage_server_addr;
                storage_server_addr.sin_family = AF_INET;
                storage_server_addr.sin_port = htons(ss_info.client_port);
                storage_server_addr.sin_addr.s_addr = inet_addr(ss_info.ip_address);
                int storage_server_socket = socket(AF_INET, SOCK_STREAM, 0);
                // connect
                if (storage_server_socket < 0)
                {
                    fprintf(stderr, "Failed to connect to storage server %d\n", ssid);
                    send_error_response(client_socket, client_req_id, "Failed to connect to storage server\n");
                    return;
                }
                if (connect(storage_server_socket, (struct sockaddr *)&storage_server_addr, sizeof(storage_server_addr)) < 0)
                {
                    fprintf(stderr, "Failed to connect to storage server %d\n", ssid);
                    send_error_response(client_socket, client_req_id, "Failed to connect to storage server\n");
                    return;
                }
                fprintf(stderr, "header %s\n", header);
                if (write_n_bytes(storage_server_socket, header, 30) != 30)
                {

                    fprintf(stderr, "Failed to send request to storage server %d\n", ssid);
                    send_error_response(client_socket, client_req_id, "Failed to send request to storage server\n");
                    // close(storage_server_socket);
                    return;
                }

                if (write_n_bytes(storage_server_socket, content, content_length) != (ssize_t)content_length)
                {
                    fprintf(stderr, "Failed to send request to storage server %d\n", ssid);
                    send_error_response(client_socket, client_req_id, "Failed to send request to storage server\n");
                }

                // close(storage_server_socket);
            }
        }
    }
    // Prepare header

    // send header along file path to the storage server

    // char *content_copy = strdup(content);
}

int copy_file(char *srcpath, int src_socket, char *destfolder, char *dest_ip, int dest_port, int reqid)
{
    char header[31] = {0};
    char *filename = (char *)strrchr(srcpath, '/');
    if (filename[strlen(filename) - 1] == '\n')
    {
        filename[strlen(filename) - 1] = '\0';
    }
    char *destpath = malloc(strlen(destfolder) + strlen(filename) + 1);
    snprintf(destpath, strlen(destfolder) + strlen(filename), "%s%s", destfolder, filename);
    destpath[strlen(destfolder) + strlen(filename)] = '\0';
    char *content = (char *)malloc(strlen(srcpath) + 1 + strlen(destpath) + 1 + strlen(dest_ip) + 1 + 10 + 1 + 1);
    sprintf(content, "%s\n%s\n%s\n%d\n", srcpath, destpath, dest_ip, dest_port);
    content[strlen(content) + 1] = '\0';
    header[0] = '7'; // copy
    snprintf(&header[1], 9, "%d", reqid);
    snprintf(&header[10], 20, "%d", strlen(content));

    if (write_n_bytes(src_socket, header, 30) != 30 || write_n_bytes(src_socket, content, strlen(content)) != strlen(content))
    {
        close(src_socket);
        return -1;
    }
    int bytes_read = read_n_bytes(src_socket, header, 30);
    if (bytes_read != 30)
    {
        close(src_socket);
        return -1;
    }
    if (header[0] == '0')
    {
        close(src_socket);
        return 0;
    }
    close(src_socket);
    return -1;
}

void handle_delete_request(int client_socket, int client_req_id, char *content, long content_length)
{
    // 1. Check that the last character is '\n'
    if (content_length == 0 || content[content_length - 1] != '\n')
    {
        send_error_response(client_socket, client_req_id, "Error: Request must end with a newline character\n");
        return;
    }

    // Remove the trailing '\n'
    content[content_length - 1] = '\0';
    content_length--;

    // 2. Check if it's a valid path
    char *path = content;

    pthread_mutex_lock(&trie_mutex);
    FileEntry *entry = search_path(path, root);
    pthread_mutex_unlock(&trie_mutex);

    if (entry == NULL)
    {
        send_error_response(client_socket, client_req_id, "Error: Path does not exist\n");
        return;
    }

    // 4. Check if the path is '/' (home)
    if (strcmp(path, "/") == 0)
    {
        send_error_response(client_socket, client_req_id, "Error: Cannot delete root directory\n");
        return;
    }

    int result = 0;

    // Check if it's a file or a directory
    if (entry->is_folder == 0)
    {
        // It's a file
        result = delete_file(path, entry);
    }
    else
    {
        // It's a directory
        result = delete_directory(path);
    }

    if (result < 0)
    {
        send_error_response(client_socket, client_req_id, "Error: Delete operation failed\n");
        return;
    }

    // Send success response to client
    send_success(client_socket, client_req_id, "Delete operation successful\n");
}

int send_success(int client_socket, int client_req_id, char *message)
{
    char header[31];
    char req_id_str[10];
    snprintf(req_id_str, sizeof(req_id_str), "%09d", client_req_id);

    char content_length_str[21];
    size_t content_length = strlen(message);
    snprintf(content_length_str, sizeof(content_length_str), "%020ld", content_length);

    header[0] = '0'; // Assuming '0' indicates a success acknowledgment
    strncpy(&header[1], req_id_str, 9);
    strncpy(&header[10], content_length_str, 20);
    header[30] = '\0';

    // Send header and content
    if (write_n_bytes(client_socket, header, 30) != 30 || write_n_bytes(client_socket, message, content_length) != (ssize_t)content_length)
    {
        return -1;
    }
    return 0;
}
// Function to connect to a storage server
int connect_to_storage_server(const char *ip_address, int port)
{
    int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_socket < 0)
    {
        perror("Failed to create socket");
        return -1;
    }

    struct sockaddr_in ss_addr;
    memset(&ss_addr, 0, sizeof(ss_addr));
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip_address, &ss_addr.sin_addr) <= 0)
    {
        perror("Invalid IP address");
        close(ss_socket);
        return -1;
    }

    if (connect(ss_socket, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0)
    {
        perror("Failed to connect to storage server");
        close(ss_socket);
        return -1;
    }

    return ss_socket;
}
void handle_copy_request(int client_socket, int client_req_id, char *content, long content_length)
{

    char *folderpath, *srcpath, *saveptr;
    srcpath = strtok_r(content, "\n", &saveptr);
    folderpath = strtok_r(NULL, "\n", &saveptr);
    if (folderpath[strlen(folderpath) - 1] == '\n')
    {
        folderpath[strlen(folderpath) - 1] = '\0';
    }
    if (folderpath[strlen(folderpath) - 1] != '/')
    {
        send_error_response(client_socket, client_req_id, "Error: Invalid folder path to copy into\n"); // destpath should be a folder
        return;
    }
    if (search_path(folderpath, root) == NULL)
    {
        send_error_response(client_socket, client_req_id, "Error: Destination folder does not exist\n");
        return;
    }
    if (search_path(srcpath, root) == NULL)
    {
        send_error_response(client_socket, client_req_id, "Error: Source path does not exist\n");
        return;
    }
    if (srcpath[strlen(srcpath) - 1] == '/')
    {
        // copy folder
    }

    else
    {
        // copy file
        // copy_file(srcpath, folderpath);
        StorageServerInfo source_info[3];
        int ss_id[3];
        int num_chosen;
        choose_least_full_servers(ss_id, &num_chosen);
        FileEntry *src_file = search_path(srcpath, root);

        if (num_chosen <= 0)
        {
            send_error_response(client_socket, client_req_id, "Error: No storage servers available\n");
            return;
        }
        int num_successful = 0;

        int ss_socket;

        for (int i = 0; i < num_chosen; i++)
        {
            // connect to storage server with id src_file->ss_ids[j]

            for (int j = 0; j < 3; j++)
            {
                if (src_file->ss_ids[j] <= 0)
                {
                    continue;
                }
                ss_socket = connect_to_storage_server(storage_servers[src_file->ss_ids[j]].ip_address, storage_servers[src_file->ss_ids[j]].port); // WRITE CODE FOR THIS
                if (ss_socket < 0)
                {
                    continue;
                }

                if (copy_file(srcpath, ss_socket, folderpath, storage_servers[ss_id[i]].ip_address, storage_servers[ss_id[i]].port, client_req_id) < 0)
                {
                    // failed to copy
                    continue;
                }

                // now, we need to send from src_file->ss_ids[j] to ss_id[i]  // I am telling src_file->ss_ids[j] to copy src_file into ss_id[i] with destfilepath
                // send to src_file->ss_ids[j]: (content)
                // src_path
                // dest_path (folderpath/filename)
                // ssip ss_id[i]
                // port ss_id[i]

                num_successful++;
                break;
            }

            close(ss_socket);
        }
        if (num_successful == num_chosen)
        {
            char *filename = (char *)strrchr(srcpath, '/');
            if (filename[strlen(filename) - 1] == '\n')
            {
                filename[strlen(filename) - 1] = '\0';
            }
            char *destpath = malloc(strlen(folderpath) + strlen(filename) + 1);
            snprintf(destpath, strlen(folderpath) + strlen(filename), "%s%s", folderpath, filename);
            destpath[strlen(folderpath) + strlen(filename)] = '\0';
            insert_path(destpath, ss_id, num_chosen, root);
            fprintf(stderr, "File copied successfully\n");
            if (send_success(client_socket, client_req_id, "File copied successfully\n") < 0)
            {
                fprintf(stderr, "Failed to send success message to client\n");
                return;
            }
            return;
        }
        else
        {
            return send_error_response(client_socket, client_req_id, "Error: Failed to copy file to storage server\n");
        }
    }

    // check for last character being "\n"
    // check if it is a valid path
    // check if it is a file, if it is then send copy request to the three (or how many ever) storage servers along with the ssip and port number for the ss to copy it from, along with last modified time
    // if it is a folder, find all the files under it in the trie, then send copy request for each of the files. for each of them insert an entry in the trie, once it is successfully copied to all three backup storage servers
}

void handle_info_request(int client_socket, int client_req_id, char *content, long content_length)
{
    fprintf(stderr, "Handling INFO request for path: %s\n", content);

    // Check if the path ends with '/'
    if (content[content_length - 1] == '/' || (content[content_length - 1] == '\n' && content[content_length - 2] == '/'))
    {
        fprintf(stderr, "Path is a directory, cannot get info\n");
        send_error_response(client_socket, client_req_id, "Error: Cannot get info of a directory\n");
        return;
    }

    // Search for the file in the trie
    FileEntry *file = search_path(content, root);
    if (file == NULL)
    {
        fprintf(stderr, "File does not exist\n");
        send_error_response(client_socket, client_req_id, "Error: File does not exist\n");
        return;
    }

    // Get storage server info (use the first available server)
    for (int i = 0; i < 3; i++)
    {
        int ssid = file->ss_ids[i];
        if (ssid < 0)
            continue;
        StorageServerInfo ss_info = storage_servers[ssid];

        // Prepare header for storage server
        char header[31] = {0}; // 30 bytes + null terminator
        char req_id_str[10] = {0};
        char content_length_str[21] = {0};
        memset(header, 0, sizeof(header));

        char operation_type = '4';         // '4' for INFO request
        int path_length = strlen(content); // not including null terminator

        // Format request ID and content length
        snprintf(req_id_str, sizeof(req_id_str), "%d", client_req_id);
        snprintf(content_length_str, sizeof(content_length_str), "%d", path_length);

        // Construct header: operation type, request ID, content length
        header[0] = operation_type;
        strncpy(&header[1], req_id_str, 9);
        strncpy(&header[10], content_length_str, 20);
        header[30] = '\0';

        // Connect to the storage server
        struct sockaddr_in storage_server_addr;
        storage_server_addr.sin_family = AF_INET;
        storage_server_addr.sin_port = htons(ss_info.port);
        storage_server_addr.sin_addr.s_addr = inet_addr(ss_info.ip_address);

        int storage_server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (storage_server_socket < 0)
        {
            fprintf(stderr, "Failed to create socket to storage server %d\n", ssid);
            // send_error_response(client_socket, client_req_id, "Error: Failed to connect to storage server\n");
            // return;
            continue;
        }
        if (connect(storage_server_socket, (struct sockaddr *)&storage_server_addr, sizeof(storage_server_addr)) < 0)
        {
            fprintf(stderr, "Failed to connect to storage server %d\n", ssid);
            // send_error_response(client_socket, client_req_id, "Error: Failed to connect to storage server\n");
            close(storage_server_socket);
            continue;
            // return;
        }

        // Send header and file path to storage server
        if (write_n_bytes(storage_server_socket, header, 30) != 30 ||
            write_n_bytes(storage_server_socket, content, path_length) != (ssize_t)path_length)
        {
            fprintf(stderr, "Failed to send request to storage server %d\n", ssid);
            // send_error_response(client_socket, client_req_id, "Error: Failed to send request to storage server\n");
            close(storage_server_socket);
            // return;
            continue;
        }

        // Receive response header from storage server
        char response_header[31]; // 30 bytes + null terminator
        ssize_t bytes_received = read_n_bytes(storage_server_socket, response_header, 30);
        if (bytes_received != 30)
        {
            fprintf(stderr, "Failed to read response header from storage server %d\n", ssid);
            // send_error_response(client_socket, client_req_id, "Error: Failed to receive response from storage server\n");
            close(storage_server_socket);
            // return;
            continue;
        }
        response_header[30] = '\0';

        // Parse response header
        char ack_code = response_header[0]; // '0' for success, '1' for error
        char ss_req_id_str[10];
        char ss_content_length_str[21];
        strncpy(ss_req_id_str, &response_header[1], 9);
        ss_req_id_str[9] = '\0';
        strncpy(ss_content_length_str, &response_header[10], 20);
        ss_content_length_str[20] = '\0';
        int ss_content_length = atoi(ss_content_length_str);

        // Read content from storage server
        char *ss_content = malloc(ss_content_length);
        if (ss_content == NULL)
        {
            fprintf(stderr, "Failed to allocate memory for storage server content\n");
            // send_error_response(client_socket, client_req_id, "Error: Memory allocation failed\n");
            close(storage_server_socket);
            // return;
            continue;
        }

        bytes_received = read_n_bytes(storage_server_socket, ss_content, ss_content_length);
        if (bytes_received != ss_content_length)
        {
            fprintf(stderr, "Failed to read content from storage server\n");
            // send_error_response(client_socket, client_req_id, "Error: Failed to receive complete response from storage server\n");
            free(ss_content);
            close(storage_server_socket);
            // return;
            continue;
        }
        // ss_content[ss_content_length] = '\0';

        // Close connection to storage server
        close(storage_server_socket);

        // Adjust the request ID in the response header to match the client's request ID
        // strncpy(&response_header[1], req_id_str, 9);

        // Send response back to the client
        if (write_n_bytes(client_socket, response_header, 30) != 30 ||
            write_n_bytes(client_socket, ss_content, ss_content_length) != (ssize_t)ss_content_length)
        {
            fprintf(stderr, "Failed to send response to client\n");
            free(ss_content);
            return;
        }

        fprintf(stderr, "INFO response sent to client successfully\n");
        free(ss_content);
        return;
    }

    send_error_response(client_socket, client_req_id, "Error: Failed to coommunicate successfully with storage server\n");
    return;
}

// FOR LIST

void handle_list_request(int client_socket, int client_req_id, char *content, long content_length)
{
    // Ensure the content is null-terminated
    char *folder_path = malloc(content_length + 1);
    if (!folder_path)
    {
        send_error_response(client_socket, client_req_id, "Error: Memory allocation failed\n");
        return;
    }
    memcpy(folder_path, content, content_length);
    folder_path[content_length] = '\0';

    // 1. Determine whether the folder exists
    TrieNode *folder_node = search_trie_node(folder_path, root);
    if (folder_node == NULL)
    {
        send_error_response(client_socket, client_req_id, "Error: Folder does not exist\n");
        free(folder_path);
        return;
    }

    // 2. Collect all paths under the folder
    char *response_content = NULL;
    size_t response_content_length = 0;

    list_paths(folder_node, folder_path, &response_content, &response_content_length);

    if (response_content == NULL || response_content_length == 0)
    {
        send_error_response(client_socket, client_req_id, "Error: No files or folders found\n");
        free(folder_path);
        return;
    }

    // 3. Prepare the response header
    char header[31];
    memset(header, '\0', sizeof(header)); // Initialize header to '\0'

    char req_id_str[10];
    char content_length_str[21];

    snprintf(req_id_str, sizeof(req_id_str), "%09d", client_req_id);
    snprintf(content_length_str, sizeof(content_length_str), "%020zu", response_content_length);

    header[0] = '0'; // Assuming '0' indicates success
    strncpy(&header[1], req_id_str, 9);
    strncpy(&header[10], content_length_str, 20);

    // 4. Send the response header and content to the client
    if (write_n_bytes(client_socket, header, 30) != 30 ||
        write_n_bytes(client_socket, response_content, response_content_length) != (ssize_t)response_content_length)
    {
        fprintf(stderr, "Failed to send response to client\n");
    }

    // Clean up
    free(folder_path);
    free(response_content);
}

void collect_paths(TrieNode *node, char *current_path, int depth, char **output, size_t *output_length)
{
    if (node == NULL)
        return;

    if (node->file_entry)
    {
        current_path[depth] = '\0';

        // Append the current path to the output
        size_t path_length = strlen(current_path);
        size_t new_length = *output_length + path_length + 1; // +1 for newline

        char *temp = realloc(*output, new_length + 1); // +1 for null terminator
        if (!temp)
        {
            fprintf(stderr, "Memory allocation failed\n");
            return;
        }
        *output = temp;

        memcpy(*output + *output_length, current_path, path_length);
        (*output)[*output_length + path_length] = '\n';
        (*output)[new_length] = '\0';

        *output_length = new_length;
    }

    for (int i = 0; i < 256; i++)
    {
        if (node->children[i])
        {
            if (depth + 1 >= MAX_PATH_LENGTH)
            {
                fprintf(stderr, "Path too long\n");
                continue;
            }

            current_path[depth] = (char)i;
            collect_paths(node->children[i], current_path, depth + 1, output, output_length);
        }
    }
}

// Recursive function to collect all paths under a folder node
void list_paths(TrieNode *node, const char *base_path, char **output, size_t *output_length)
{
    if (node == NULL)
        return;

    char path_buffer[MAX_PATH_LENGTH];
    strcpy(path_buffer, base_path);

    collect_paths(node, path_buffer, strlen(base_path), output, output_length);
}

// END FOR LIST

// Register a Storage Server
int register_storage_server(const char *ip, int port_c, int port_ns)
{
    int id = storage_server_count++;
    storage_servers[id].id = id;
    strcpy(storage_servers[id].ip_address, ip);
    storage_servers[id].port = port_ns;
    storage_servers[id].client_port = port_c;
    storage_servers[id].file_count = 0;
    printf("Registered Storage Server %d: %s:%d\n", id, ip, port_ns);
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

    // get port through which sotrage server is communicating
    int port_ns = ntohs(addr.sin_port);

    // Register storage server
    pthread_mutex_lock(&storage_server_mutex);
    int ss_id = register_storage_server(ip, port, port_ns);
    pthread_mutex_unlock(&storage_server_mutex);

    // Process the accumulated paths and insert them into the trie
    char *saveptr1;
    char *saveptr2;
    char *line = strtok_r(paths, "\n", &saveptr1);
    while (line != NULL)
    {
        // Insert the path into the trie
        // choose least full servers
        // Split the line into path and timestamp
        char *path = strtok_r(line, " ", &saveptr2);
        char *timestamp = strtok_r(NULL, " ", &saveptr2);
        int chosen_servers[3];
        int num_chosen = 0;
        choose_least_full_servers(chosen_servers, &num_chosen);
        insert_path(path, chosen_servers, num_chosen, root);
        // Move to the next path

        if (path && timestamp)
        {
            // Insert the path into the trie
            FileEntry *file = insert_path(path, &ss_id, 1, root);
            if (file)
            {
                file->is_folder = 0;
            }

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

    // struct timeval timeout;
    // timeout.tv_sec = 5;
    // timeout.tv_usec = 0;

    // setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
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
        char *data_buffer = malloc(content_len + 1);
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
        // total_bytes_read = recv(client_socket, data_buffer, content_len, 0);

        // if (total_bytes_read < content_len){
        //     fprintf(stderr, "Failed to read data\n");
        //     free(data_buffer);
        //     close(client_socket);
        //     return NULL;
        // }
        if (data_buffer[content_len - 1] == '\0')
        {
            fprintf(stderr, "null character because");
        }
        // data_buffer[content_len-1] = '\0'; // Null-terminate the data
        data_buffer[content_len] = '\0'; // Null-terminate the data
        // fprintf(stderr, "Received data: %s\n", data_buffer);
        for (int i = 0; i < content_len; i++)
        {
            fprintf(stderr, "%c", data_buffer[i]);
        }
        fprintf(stderr, "\n");

        // Process the DATA
        // First 5 bytes are the port number
        char port_str[6];
        memcpy(port_str, data_buffer, 5);
        port_str[5] = '\0'; // Null-terminate the port string
        int port = atoi(port_str);

        // Remaining data contains strings (paths)
        char *remaining_data = data_buffer + 5;
        // size_t remaining_length = content_len - 5;
        fprintf(stderr, "Remaining data: ");
        for (int i = 0; i < sizeof(remaining_data); i++)
        {
            if (remaining_data[i] == '\0')
            {
                // fprintf(stderr,"null character");
                remaining_data[i] = ' ';
            }
            fprintf(stderr, "%c", remaining_data[i]);
        }
        fprintf(stderr, "\n");

        // Accumulate the required strings (skip every second string)
        char accumulated_paths[16384]; // Adjust size as needed
        size_t accumulated_length = 0;

        char *line;
        char *saveptr1;
        line = strtok_r(remaining_data, "\n", &saveptr1);
        fprintf(stderr, "saveptr: ");
        for (int i = 0; i < sizeof(saveptr1); i++)
        {
            fprintf(stderr, "%c", saveptr1[i]);
        }
        fprintf(stderr, "\n");
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
            char *path;
            char *skip;
            char *timestamp;
            char *saveptr2;

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
            // recieve header of 30 bytes with 1 byte request type, 9 bytes request id, 20 bytes content length for acknowledgment
            char header[31];
            bytes_received = read_n_bytes(client_socket, header, 30);
            if (bytes_received != 30)
            {
                fprintf(stderr, "Failed to read header from storage server\n");
                close(client_socket);
                return NULL;
            }
            header[30] = '\0';
            // Parse header fields
            char request_type;
            char req_id_str[10];
            char content_length_str[21];
           
            request_type = header[0];
            strncpy(req_id_str, &header[1], 9);
            req_id_str[9] = '\0';
            strncpy(content_length_str, &header[10], 20);
            content_length_str[20] = '\0';
            int content_length = atoi(content_length_str);
            fprintf(stderr, "Received request from storage server\n");
            fprintf(stderr, "request type: %c\n", request_type);
            fprintf(stderr, "request id: %s\n", req_id_str);
            fprintf(stderr, "content length: %d\n", content_length);

            // read content from storage server
            char *content = malloc(content_length + 1);
            if (!content)
            {
                fprintf(stderr, "Failed to allocate memory for content\n");
                close(client_socket);
                return NULL;
            }
            bytes_received = read_n_bytes(client_socket, content, content_length);
            if (bytes_received != content_length)
            {
                fprintf(stderr, "Failed to read content from storage server\n");
                free(content);
                close(client_socket);
                return NULL;
            }
            content[content_length] = '\0';
            fprintf(stderr, "content: %s\n", content);
        }
    }
    else
    {
        // Handle client connections

        // fprintf(stderr, "Received request from client\n");
        fprintf(stderr, "request type: %c\n", request_type);
        handle_client(client_socket, request_type);
        close(client_socket);
    }

    return NULL;
}

void handle_client(int client_socket, char initial_request_type)
{
    while (1)
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
            // close(client_socket);
            return;
        }
        // fprintf(stderr, "blalalal\n");
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
        fprintf(stderr, "client_req_id: %d\n", client_req_id);
        fprintf(stderr, "content_length: %ld\n", content_length);

        // Read content from client
        char *content = malloc(content_length + 1);
        if (!content)
        {
            fprintf(stderr, "Failed to allocate memory for content\n");
            // close(client_socket);
            return;
        }

        bytes_received = read_n_bytes(client_socket, content, content_length);
        if (bytes_received != content_length)
        {
            fprintf(stderr, "Failed to read content from client\n");
            free(content);
            // close(client_socket);
            return;
        }
        content[content_length] = '\0';
        fprintf(stderr, "content: %s\n", content);

        pthread_mutex_lock(&global_req_id_mutex);
        int storage_req_id = global_req_id++;
        FILE *fd = fopen("requests.txt", "a");
        if (fd == NULL)
        {
            perror("Error opening requests.txt");
            free(content);
            pthread_mutex_unlock(&global_req_id_mutex);
            return;
        }

        char file_write_buffer[256];
        int len = snprintf(file_write_buffer, sizeof(file_write_buffer), "%d request: id: %c req_id: %s content_length %s\n", storage_req_id, header[0], id_str, content_length_str);
        fwrite(file_write_buffer, 1, len, fd);

        fclose(fd);
        pthread_mutex_unlock(&global_req_id_mutex);

        client_req_id = storage_req_id;

        // Handle the request based on request_type
        if (request_type == '6') // '6' for CREATE
        {
            fprintf(stderr, "Received CREATE request from client\n");
            handle_create_request(client_socket, client_req_id, content, content_length);
        }
        else if (request_type == '1' || request_type == '3' || request_type == '2')
        {
            fprintf(stderr, "Received READ/WRITE/STREAM request from client\n");
            handle_rws_request(client_socket, client_req_id, content, content_length, request_type);
        }
        else if (request_type == '4')
        {
            fprintf(stderr, "Received INFO request from client\n");
            handle_info_request(client_socket, client_req_id, content, content_length);
        }
        else if (request_type == '5')
        {
            fprintf(stderr, "Received LIST request from client\n");
            handle_list_request(client_socket, client_req_id, content, content_length);
        }
        else if (request_type == '7')
        {
            fprintf(stderr, "Received COPY request from client\n");
            handle_copy_request(client_socket, client_req_id, content, content_length);
        }
        else if (request_type == '8')
        {
            fprintf(stderr, "Received DELETE request from client\n");
            handle_delete_request(client_socket, client_req_id, content, content_length);
        }
        else
        {
            fprintf(stderr, "Invalid request type received: %c\n", request_type);
            send_error_response(client_socket, client_req_id, "Error: Invalid request type\n");
        }
        free(content);
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
    // make the root a file entry /
    root->file_entry = (FileEntry *)malloc(sizeof(FileEntry));
    root->file_entry->is_folder = 1;
    strcpy(root->file_entry->filename, "/");

    // Initialize Cache
    cache = init_cache(CACHE_SIZE);

    // Load Trie and Cache from files
    load_trie("trie_data.bin", root);

    load_cache("cache_data.bin", cache);

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
    // pthread_join(accept_thread, NULL);
    pthread_exit(NULL);

    sem_destroy(&storage_server_sem);
    pthread_mutex_destroy(&storage_server_mutex);

    return 0;
}
