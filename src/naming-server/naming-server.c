#include "main.h"
#include "../lib/request.h"

// f Global port
TrieNode *root;
StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
int storage_server_count = 0;
struct lru_cache *cache;              // Cache pointer
sem_t storage_server_sem;             // Semaphore to track storage servers
pthread_mutex_t storage_server_mutex; // Mutex to protect storage_server_count
int round_robin_counter = 0;
pthread_mutex_t global_req_id_mutex; // Mutex to protect global request ID
int global_req_id = 2;
pthread_mutex_t trie_mutex; // Mutex to protect trie operations

void handle_create_request(int client_socket, int client_req_id, char *content, long content_length);
int delete_file(const char *path, FileEntry *entry, int client_req_id);
int send_success(int client_socket, int client_req_id, char *message);
void list_paths(TrieNode *node, const char *base_path, char ***output, size_t *output_length);
int connect_to_storage_server(const char *ip_address, int port);

clientData request_array[MAX_CLIENTS];

TrieNode *search_trie_node(const char *path, TrieNode *root)
{
    TrieNode *current_node = root;
    const char *p = path;

    if (strlen(path) == 1)
    {
        if (path[0] == '/')
        {
            return root;
        }
    }

    while (*p)
    {
        unsigned char index = (unsigned char)*p;
        if (!current_node->children[index])
            return NULL;
        current_node = current_node->children[index];
        p++;
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
void send_error_response(int client_socket, int req_id, enum exit_status status)
{
    respond(client_socket, -1, status, req_id, 0, NULL, 0);
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
            // get ip and port from socket_fd and check storage server list , and set offline to 1
            // get ip and port
            struct sockaddr_in addr;
            socklen_t addr_len = sizeof(addr);
            getpeername(socket_fd, (struct sockaddr *)&addr, &addr_len);
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
            int port = ntohs(addr.sin_port);
            // check storage server list
            for (int i = 0; i < storage_server_count; i++)
            {
                if (strcmp(storage_servers[i].ip_address, ip_str) == 0 && storage_servers[i].port == port)
                {
                    storage_servers[i].offline = 1;
                    fprintf(stderr, "Storage server id %d, %s:%d is offline\n", i, ip_str, port);
                    break;
                }
            }
            return -1;
        }
        total_read += bytes_read;
    }
    return total_read;
}

int read_request_header(int socket_fd, request_header *header)
{
    ssize_t bytes_read = recv(socket_fd, header, sizeof(*header), MSG_WAITALL);
     if (bytes_read < 0) {
        perror("recv failed");
        return -1;
    }
    if (bytes_read < (ssize_t)sizeof(*header)) {
        fprintf(stderr, "Incomplete request header read: %ld out of %ld\n", bytes_read, sizeof(*header));
        return -1;
    }
    return 0;
}

int read_response_header(int socket_fd, response_header *header)
{
    ssize_t bytes_read = recv(socket_fd, header, sizeof(*header), 0);
     if (bytes_read < 0) {
        perror("recv failed");
        return -1;
    }
    if (bytes_read < (ssize_t)sizeof(*header)) {
        fprintf(stderr, "Incomplete request header read\n");
        return -1;
    }
    return 0;
}

void choose_least_full_servers(int *chosen_servers, int *num_chosen)
{
    int min_file_counts[3] = {__INT_MAX__, __INT_MAX__, __INT_MAX__};
    chosen_servers[0] = -1;
    chosen_servers[1] = -1;
    chosen_servers[2] = -1;
    // choose 3 least filled storage servers, not duplicate, if there are less than 3, choose least 2 or 1, keep rest as -1 and num_chosen as 2 or 1
    printf("storage server count: %d\n", storage_server_count);
    for (int i = 0; i < storage_server_count; i++)
    {
        if (storage_servers[i].file_count < min_file_counts[0])
        {
            min_file_counts[2] = min_file_counts[1];
            min_file_counts[1] = min_file_counts[0];
            min_file_counts[0] = storage_servers[i].file_count;
            chosen_servers[2] = chosen_servers[1];
            chosen_servers[1] = chosen_servers[0];
            chosen_servers[0] = i;
        }
        else if (storage_servers[i].file_count < min_file_counts[1])
        {
            min_file_counts[2] = min_file_counts[1];
            min_file_counts[1] = storage_servers[i].file_count;
            chosen_servers[2] = chosen_servers[1];
            chosen_servers[1] = i;
        }
        else if (storage_servers[i].file_count < min_file_counts[2])
        {
            min_file_counts[2] = storage_servers[i].file_count;
            chosen_servers[2] = i;
        }
    }
    if (chosen_servers[0] == -1)
    {
        *num_chosen = 0;
    }
    else if (chosen_servers[1] == -1)
    {
        *num_chosen = 1;
    }
    else if (chosen_servers[2] == -1)
    {
        *num_chosen = 2;
    }
    else
    {
        *num_chosen = 3;
    }
    printf("chosen servers: %d %d %d\n", chosen_servers[0], chosen_servers[1], chosen_servers[2]);
}

void signal_handler(int sig)
{
    printf("Received signal %d, saving data and exiting...\n", sig);
    save_trie("trie_data.bin", root);
    save_cache("cache_data.bin", cache);
    exit(0);
}

void handle_rsi_request(int client_socket, int client_req_id, char *path, enum request_type type)
{
    // char *path_buffer = malloc(content_length + 1);
    // if (path_buffer == NULL)
    // {
    //     fprintf(stderr, "Failed to allocate memory for data buffer\n");
    //     // close(client_socket);
    //     return;
    // }
    // int total_read = 0;
    // fprintf(stderr, "content_length: %ld\n", content_length);
    // fprintf(stderr, "content: %s\n", content);
    // content[content_length-1] = '\0';

    FileEntry *file = search_path(path, root);

    // check if request type is 3 and then if yes, check if the file contains .pcm or .mpe
    if (type == INFO)
    {
        if (strstr(path, ".pcm") == NULL && strstr(path, ".mp3") == NULL)
        {
            fprintf(stderr, "file does not contain .pcm or .mp3\n");
            send_error_response(client_socket, client_req_id, E_INVALID_FILE);
            return;
        }
    }
    if (file == NULL)
    {
        fprintf(stderr, "path not found\n");
        send_error_response(client_socket, client_req_id, E_FILE_DOESNT_EXIST);
    }
    else
    {
        if(file->is_folder == 1){
            send_error_response(client_socket, client_req_id, E_INVALID_FILE);
            fprintf(stderr, "path is a folder, not a file\n");
            return;
        }
        // Get storage server information
        // check first online storage server
        //ahhh
        int id = -1;
        for (int i = 0; i < 3; i++)
        {
            if (file->ss_ids[i] != -1 && storage_servers[file->ss_ids[i]].offline == 0)
            {
                id = file->ss_ids[i];
                break;
            }
        }
        if(id == -1){
            fprintf(stderr, "No online storage servers found for file\n");
            send_error_response(client_socket, client_req_id, E_WRONG_SS);
            return;
        }
        StorageServerInfo ss_info = storage_servers[id];
        // Prepare response content with IP and Port
        char response_content[256] = {0};
        snprintf(response_content, sizeof(response_content), "%s\n%d\n", ss_info.ip_address, ss_info.client_port);
        size_t response_content_length = strlen(response_content);

        respond(client_socket, -1, SUCCESS, client_req_id, 0, ss_info.ip_address, ss_info.client_port);

        fprintf(stderr, "Sent storage server info to client: IP=%s, Port=%d\n", ss_info.ip_address, ss_info.client_port);
    }

    fprintf(stderr, "Handled rsi request %d %s %d\n", client_req_id, path, type);
}

// void handle_write_request(int client_socket, int client_req_id, char* content, long content_length){
//     char *path_buffer = malloc(content_length + 1);
//     if (path_buffer == NULL)
//     {
//         fprintf(stderr, "Failed to allocate memory for data buffer\n");
//         // close(client_socket);
//         return;
//     }
//     int total_read = 0;
//     fprintf(stderr, "content_length: %ld\n", content_length);
//     fprintf(stderr, "content: %s\n", content);
//     // content[content_length-1] = '\0';

//     FileEntry *file = search_path(content, root);
//     if (file == NULL)
//     {
//         fprintf(stderr, "path not found\n");
//         send_error_response(client_socket, client_req_id, "path not found\n");
//     } 
//     else {
//         // Collect storage server info
//         // check if any storage server is offline
//         int offline = 0;
//         for (int i = 0; i < 3; i++)
//         {
//             if(file->ss_ids[i] == -1){
//                 continue;
//             }
//             if (storage_servers[file->ss_ids[i]].offline == 1)
//             {
//                 offline = 1;
//                 break;
//             }
//         }
//         if(offline == 1){
//             fprintf(stderr, "One or more storage servers are offline\n");
//             send_error_response(client_socket, client_req_id, "One or more storage servers are offline\n");
//             free(path_buffer);
//             return;
//         }
//         StorageServerInfo ss_info[3];
//         int server_count = 0;
//         for (int i = 0; i < 3; i++)
//         {
//             if (file->ss_ids[i] != -1)
//             {
//                 int ss_index = file->ss_ids[i];
//                 ss_info[server_count++] = storage_servers[ss_index];
//             }
//         }

//         if (server_count == 0)
//         {
//             fprintf(stderr, "No storage servers found for file\n");
//             send_error_response(client_socket, client_req_id, "No storage servers found for file\n");
//             free(path_buffer);
//             return;
//         }

//         // Prepare response
//         char response[1024] = {0};
//         for (int i = 0; i < server_count; i++)
//         {
//             char server_info[256];
//             snprintf(server_info, sizeof(server_info), "%s\n%d\n", ss_info[i].ip_address, ss_info[i].client_port);
//             strncat(response, server_info, sizeof(response) - strlen(response) - 1);
//         }

//         // Send response to client
//         send_success(client_socket, client_req_id, response);
//         fprintf(stderr, "Sent storage server info to client: %s\n", response);
//     }

//     free(path_buffer);
//     fprintf(stderr, "Handled write request %d %s %ld\n", client_req_id, content, content_length);
// }


int delete_file(const char *path, FileEntry *entry, int client_req_id)
{
    int storage_req_id = client_req_id;
    int success_count = 0;

    // Send delete request to all storage servers that have the file
    for (int i = 0; i < 3; i++)
    {
        int ss_id = entry->ss_ids[i];
        if (ss_id == -1)
            continue;

        StorageServerInfo ss_info = storage_servers[ss_id];

        int ss_socket = connect_to_storage_server(ss_info.ip_address, ss_info.client_port);
        if (ss_socket < 0)
        {
            fprintf(stderr, "Failed to connect to storage server %d\n", ss_id);
            continue;
        }

        // Prepare header
        char header[31] = {0};
        char req_id_str[10];
        char content_length_str[21];

        snprintf(req_id_str, sizeof(req_id_str), "%09d", storage_req_id);
        fprintf(stderr, "path length: %ld\n", strlen(path));
        snprintf(content_length_str, sizeof(content_length_str), "%020ld", strlen(path) + 1);
        // content_length_str[20] = '/n';
        char new_path[strlen(path) + 1];
        snprintf(new_path, sizeof(new_path), "%s\n", path);
        fprintf(stderr, "new path: %s\n", new_path);

        header[0] = '8'; // '7' for DELETE operation
        strncpy(&header[1], req_id_str, 9);
        strncpy(&header[10], content_length_str, 20);
        fprintf(stderr, "header: %s\n", header);

        // Send header and path
        if (write_n_bytes(ss_socket, header, 30) != 30 ||
            write_n_bytes(ss_socket, new_path, strlen(path) + 1) != (ssize_t)(strlen(path) + 1))
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

// void handle_create_request(int client_socket, int client_req_id, char *content, long content_length)
// {
//     // Parse content into folderpath and name
//     char *folderpath = (char *)malloc(content_length + 1);
//     char *name = (char *)malloc(content_length + 1);
//     fprintf(stderr, "content: %s\n", content);

//     char *saveptr;
//     char *exist_folder = strtok_r(content, "\n", &saveptr);
//     fprintf(stderr, "exist_folder: %s\n", exist_folder);

//     FileEntry *file = search_path(exist_folder, root);
//     if (file == NULL) kkkk
//     {
//         fprintf(stderr, "Folder does not exist\n");
//         send_error_response(client_socket, client_req_id, "Error: Folder does not exist\n");
//         return;
//     }
//     else if (file->is_folder == 0)
//     {
//         fprintf(stderr, "Path is not a folder\n");
//         send_error_response(client_socket, client_req_id, "Error: Path is not a folder\n");
//         return;
//     }
//     else
//     {
//         fprintf(stderr, "Folder exists\n");
//         char *to_create = strtok_r(NULL, "\n", &saveptr);
//         fprintf(stderr, "to_create: %s\n", to_create);
//         char file_path[4096];
//         snprintf(file_path, sizeof(file_path), "%s%s", exist_folder, to_create);
//         file_path[strlen(file_path)] = '\0';
//         if (to_create[strlen(to_create) - 1] == '/')
//         {
//             char success = '0';
//             int chosen_servers[3];
//             int num_chosen = 0;
//             choose_least_full_servers(chosen_servers, &num_chosen);
//             FileEntry *file = insert_path_forc(file_path, chosen_servers, num_chosen, root);
//             if (file != NULL)
//             {
//                 success = '0';
//                 file->is_folder = 1;
//             }
//             else
//             {
//                 success = '1';
//             }
//             // send success response to client, 30 byte header, 1st byte is the success byte, 9 bytes request id, 20 bytes content length
//             char header[31]; // 30 bytes + null terminator
//             char req_id_str[10];
//             char content_length_str[21];
//             memset(header, 0, sizeof(header));
//             memset(req_id_str, 0, sizeof(req_id_str));
//             memset(content_length_str, 0, sizeof(content_length_str));
//             // add content length to content length string as character
//             snprintf(content_length_str, sizeof(content_length_str), "%d", 0);
//             fprintf(stderr, "content_length_str: %s\n", content_length_str);
//             header[0] = success;
//             // set request id string to client_req_id
//             snprintf(req_id_str, sizeof(req_id_str), "%09d", client_req_id);
//             fprintf(stderr, "req_id_str: %s\n", req_id_str);
//             // set header to success byte, request id and content length
//             strncpy(&header[1], req_id_str, strlen(req_id_str));
//             strncpy(&header[10], content_length_str, strlen(content_length_str));
//             header[30] = '\n';
//             fprintf(stderr, "header: %s\n", header);
//             // send header to client
//             if (write_n_bytes(client_socket, header, 30) != 30)
//             {
//                 fprintf(stderr, "Failed to send response to client\n");
//                 return;
//             }
//         }
//         else
//         {

//             // set timestamp
//             time_t t = time(NULL);
//             struct tm tm = *localtime(&t);
//             char timestamp[20];
//             memset(timestamp, 0, sizeof(timestamp));
//             strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm);
//             fprintf(stderr, "timestamp: %s\n", timestamp);
//             int chosen_servers[3];
//             int num_chosen = 0;
//             choose_least_full_servers(chosen_servers, &num_chosen);
//             FileEntry *file = insert_path_forc(file_path, chosen_servers, num_chosen, root);
//             file->is_folder = 0;
//             set_file_entry_timestamp(file, timestamp);

//             char file_path[4096];

//             size_t folderpath_len = strlen(exist_folder);
//             size_t name_len = strlen(to_create);
//             if (folderpath_len + name_len + 1 >= sizeof(file_path))
//             {
//                 fprintf(stderr, "Path too long\n");
//                 send_error_response(client_socket, client_req_id, "Error: Path too long\n");
//                 return;
//             }
//             snprintf(file_path, sizeof(file_path), "%s%s", exist_folder, to_create);
//             file_path[strlen(file_path)] = '\0';
//             fprintf(stderr, "file_path: %s\n", file_path);
//             size_t content_length = strlen(file_path) + 1 + strlen(timestamp);

//             char *content = malloc(content_length + 1);
//             if (content == NULL)
//             {
//                 fprintf(stderr, "Failed to allocate memory for content\n");
//                 send_error_response(client_socket, client_req_id, "Error: Memory allocation failed\n");
//                 return;
//             }

//             strncpy(content, file_path, strlen(file_path));
//             content[strlen(file_path)] = '\n';
//             fprintf(stderr, "content: %s\n", content);
//             strncpy(content + strlen(content), timestamp, strlen(timestamp));
//             content[content_length] = '\n';
//             fprintf(stderr, "content: %s\n", content);

//             char header[31]; // 30 bytes + null terminator
//             char req_id_str[10];
//             char content_length_str[21];
//             memset(header, 0, sizeof(header));
//             memset(req_id_str, 0, sizeof(req_id_str));
//             memset(content_length_str, 0, sizeof(content_length_str));
//             // add content length to content length string as character
//             snprintf(content_length_str, sizeof(content_length_str), "%ld", content_length);
//             fprintf(stderr, "content_length_str: %s\n", content_length_str);

//             char operation_type = '6'; // '6' for CREATE
//             // send 30 bytes header: 1 byte operation type, 9 bytes request id, 20 bytes content length
//             // generate new content length that is length of filepath + new line + timestamp size  using strnc

//             // set global request id to req_id_str use strncpy, convert global_req_id to string
//             snprintf(req_id_str, sizeof(req_id_str), "%09d", client_req_id);
//             fprintf(stderr, "req_id_str: %s\n", req_id_str);

//             header[0] = operation_type;
//             strncpy(&header[1], req_id_str, strlen(req_id_str));
//             //
//             strncpy(&header[10], content_length_str, strlen(content_length_str));
//             header[30] = '\n';
//             // send request to the 3 least filled storage servers of the file entry
//             memset(chosen_servers, -1, sizeof(chosen_servers));
//             num_chosen = 0;
//             choose_least_full_servers(chosen_servers, &num_chosen);

//             if (num_chosen == 0)
//             {
//                 send_error_response(client_socket, client_req_id, "No storage servers available\n");
//                 return;
//             }

//             for (int i = 0; i < num_chosen; i++) // what if there are less than 3 ???
//             {
//                 // send request to storage server
//                 // send header along file path to the storage server
//                 int ssid = chosen_servers[i];
//                 StorageServerInfo ss_info = storage_servers[ssid];
//                 // connect to storage server using IP and Port in the ssinfo using connect and sockaddr
//                 struct sockaddr_in storage_server_addr;
//                 storage_server_addr.sin_family = AF_INET;
//                 storage_server_addr.sin_port = htons(ss_info.client_port);
//                 storage_server_addr.sin_addr.s_addr = inet_addr(ss_info.ip_address);
//                 int storage_server_socket = socket(AF_INET, SOCK_STREAM, 0);
//                 // connect
//                 if (storage_server_socket < 0)
//                 {
//                     fprintf(stderr, "Failed to connect to storage server %d\n", ssid);
//                     send_error_response(client_socket, client_req_id, "Failed to connect to storage server\n");
//                     return;
//                 }
//                 if (connect(storage_server_socket, (struct sockaddr *)&storage_server_addr, sizeof(storage_server_addr)) < 0)
//                 {
//                     fprintf(stderr, "Failed to connect to storage server %d\n", ssid);
//                     send_error_response(client_socket, client_req_id, "Failed to connect to storage server\n");
//                     return;
//                 }
//                 fprintf(stderr, "header %s\n", header);
//                 if (write_n_bytes(storage_server_socket, header, 30) != 30)
//                 {

//                     fprintf(stderr, "Failed to send request to storage server %d\n", ssid);
//                     send_error_response(client_socket, client_req_id, "Failed to send request to storage server\n");
//                     // close(storage_server_socket);
//                     return;
//                 }

//                 if (write_n_bytes(storage_server_socket, content, content_length) != (ssize_t)content_length)
//                 {
//                     fprintf(stderr, "Failed to send request to storage server %d\n", ssid);
//                     send_error_response(client_socket, client_req_id, "Failed to send request to storage server\n");
//                 }

//                 // close(storage_server_socket);
//             }
//         }
//     }
//     // Prepare header

//     // send header along file path to the storage server

//     // char *content_copy = strdup(content);
// }

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
    snprintf(&header[10], 20, "%ld", strlen(content));

    if (write_n_bytes(src_socket, header, 30) != 30 || write_n_bytes(src_socket, content, strlen(content)) != strlen(content))
    {
        // close(src_socket);
        return -1;
    }
    int bytes_read = read_n_bytes(src_socket, header, 30);
    if (bytes_read != 30)
    {
        // close(src_socket);
        return -1;
    }
    if (header[0] == '0')
    {
        // close(src_socket);
        return 0;
    }
    // close(src_socket);
    return -1;
}

// void handle_delete_request(int client_socket, int client_req_id, char *content, long content_length)
// {
//     // 1. Check that the last character is '\n'
//     if (content_length == 0 || content[content_length - 1] != '\n')
//     {
//         send_error_response(client_socket, client_req_id, "Error: Request must end with a newline character\n");
//         return;
//     }

//     // Remove the trailing '\n'
//     content[content_length - 1] = '\0';
//     content_length--;

//     // remove the \n between folderpath and filename/foldername because format <foldername>\n<filename>
//     char *saveptr;
//     char *folderpath = strtok_r(content, "\n", &saveptr);
//     char *filename = strtok_r(NULL, "\n", &saveptr);
//     int len = strlen(filename) + strlen(folderpath) + 1;

//     // 2. Check if it's a valid path
//     // construct path as folderpath/filename
//     char path[len];
//     snprintf(path, sizeof(path), "%s%s", folderpath, filename);
//     path[len] = '\0';
//     pthread_mutex_lock(&trie_mutex);
//     FileEntry *entry = search_path(path, root);
//     pthread_mutex_unlock(&trie_mutex);

//     if (entry == NULL)
//     {
//         send_error_response(client_socket, client_req_id, "Error: Path does not exist\n");
//         return;
//     }

//     int is_folder = entry->is_folder;


//     // 4. Check if the path is '/' (home)
//     if (strcmp(path, "/") == 0)
//     {
//         send_error_response(client_socket, client_req_id, "Error: Cannot delete root directory\n");
//         return;
//     }

//     int result = 0;

//     // delete
//     remove_path(path, root);

//     // Check if it's a file or a directory
//     if (is_folder == 0)
//     {
//         // send delete header request to all storage servers that have the file
//         for (int i = 0; i < 3; i++)
//         {
//             int ss_id = entry->ss_ids[i];
//             if (ss_id == -1)
//                 continue;

//             StorageServerInfo ss_info = storage_servers[ss_id];

//             int ss_socket = connect_to_storage_server(ss_info.ip_address, ss_info.client_port);
//             if (ss_socket < 0)
//             {
//                 fprintf(stderr, "Failed to connect to storage server %d\n", ss_id);
//                 continue;
//             }

//             // Prepare header
//             char header[31] = {0};
//             char req_id_str[10];
//             char content_length_str[21];

//             snprintf(req_id_str, sizeof(req_id_str), "%09d", client_req_id);

//             // combine filepath and folderpath to get absolute path
//             char abs_path[4096];
//             snprintf(abs_path, sizeof(abs_path), "%s%s\n", folderpath, filename);
//             snprintf(content_length_str, sizeof(content_length_str), "%020ld", strlen(abs_path));

//             header[0] = '8';
//             strncpy(&header[1], req_id_str, 9);
//             strncpy(&header[10], content_length_str, 20);
//             header[30] = '\0';
//             fprintf(stderr, "header: %s\n", header);

//             // Send header and path
//             if (write_n_bytes(ss_socket, header, 30) != 30 ||
//                 write_n_bytes(ss_socket, abs_path, strlen(abs_path) + 1) != (ssize_t)(strlen(abs_path) + 1))
//             {
//                 fprintf(stderr, "Failed to send delete request to storage server %d\n", ss_id);
//                 continue;
//             }

//             // Read response
//             char response_header[31];
//             if (read_n_bytes(ss_socket, response_header, 30) != 30)
//             {
//                 fprintf(stderr, "Failed to read response from storage server %d\n", ss_id);
//                 continue;
//             }

//             if (response_header[0] == '0') // '0' indicates success
//             {
//                 result++;
//             }
//             else
//             {
//                 fprintf(stderr, "Error response from storage server %d\n", ss_id);
//             }
//         }
//     }else{
//         char header[31] = {0};
//             char req_id_str[10];
//             char content_length_str[21];

//             snprintf(req_id_str, sizeof(req_id_str), "%09d", client_req_id);

//             // combine filepath and folderpath to get absolute path
//             char abs_path[4096];
//             snprintf(abs_path, sizeof(abs_path), "%s%s\n", folderpath, filename);
//             snprintf(content_length_str, sizeof(content_length_str), "%020ld", strlen(abs_path));

//             header[0] = '8';
//             strncpy(&header[1], req_id_str, 9);
//             strncpy(&header[10], content_length_str, 20);
//             header[30] = '\0';
//             fprintf(stderr, "header: %s\n", header);

//             // Send header and path
//             if (write_n_bytes(client_socket, header, 30) != 30 ||
//                 write_n_bytes(client_socket, abs_path, strlen(abs_path) + 1) != (ssize_t)(strlen(abs_path) + 1))
//             {
//                 fprintf(stderr, "error to client\n");
//             }
//     }

//     // if (result < 0)
//     // {
//     //     send_error_response(client_socket, client_req_id, "Error: Delete operation failed\n");
//     //     return;
//     // }

//     // Send success response to client
//     // send_success(client_socket, client_req_id, "Delete operation successful\n");
// }

// function to delete node from a trie by making parent point to NULL
void delete_from_trie(char *path, struct TrieNode *root)
{
    struct TrieNode *current = root;
    char last = path[strlen(path) - 1];
    if (strlen(path) == 1)
    {
        if (path[0] == '/')
        {
            fprintf(stderr, "cannot delete root\n");
            return;
        }
    }
    if (path[0] != '/')
    {
        fprintf(stderr, "path does not start with /\n");
        return;
    }
    for (int i = 1; path[i]; i++)
    {
        unsigned char index = (unsigned char)path[i];
        if (!current->children[index])
            return;
        if (i == strlen(path) - 1)
        {
            current->children[index]->file_entry = NULL;
            current->children[index] = NULL;
            return;
        }
        current = current->children[index];
    }
    current->file_entry = NULL;
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
        // close(ss_socket);
        return -1;
    }

    if (connect(ss_socket, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0)
    {
        perror("Failed to connect to storage server");
        // close(ss_socket);
        return -1;
    }

    return ss_socket;
}
// void handle_copy_request(int client_socket, int client_req_id, char *content, long content_length)
// {

//     char *folderpath, *srcpath, *saveptr;
//     srcpath = strtok_r(content, "\n", &saveptr);
//     folderpath = strtok_r(NULL, "\n", &saveptr);
//     if (folderpath[strlen(folderpath) - 1] == '\n')
//     {
//         folderpath[strlen(folderpath) - 1] = '\0';
//     }
//     if (folderpath[strlen(folderpath) - 1] != '/')
//     {
//         send_error_response(client_socket, client_req_id, "Error: Invalid folder path to copy into\n"); // destpath should be a folder
//         return;
//     }
//     if (search_path(folderpath, root) == NULL)
//     {
//         send_error_response(client_socket, client_req_id, "Error: Destination folder does not exist\n");
//         return;
//     }
//     if (search_path(srcpath, root) == NULL)
//     {
//         send_error_response(client_socket, client_req_id, "Error: Source path does not exist\n");
//         return;
//     }
//     if (srcpath[strlen(srcpath) - 1] == '/')
//     {
//         // copy folder
//     }

//     else
//     {
//         // copy file
//         // copy_file(srcpath, folderpath);
//         StorageServerInfo source_info[3];
//         int ss_id[3];
//         int num_chosen;
//         choose_least_full_servers(ss_id, &num_chosen);
//         FileEntry *src_file = search_path(srcpath, root);

//         if (num_chosen <= 0)
//         {
//             send_error_response(client_socket, client_req_id, "Error: No storage servers available\n");
//             return;
//         }
//         int num_successful = 0;

//         int ss_socket;

//         for (int i = 0; i < num_chosen; i++)
//         {
//             // connect to storage server with id src_file->ss_ids[j]

//             for (int j = 0; j < 3; j++)
//             {
//                 if (src_file->ss_ids[j] <= 0)
//                 {
//                     continue;
//                 }
//                 ss_socket = connect_to_storage_server(storage_servers[src_file->ss_ids[j]].ip_address, storage_servers[src_file->ss_ids[j]].port); // WRITE CODE FOR THIS
//                 if (ss_socket < 0)
//                 {
//                     continue;
//                 }

//                 if (copy_file(srcpath, ss_socket, folderpath, storage_servers[ss_id[i]].ip_address, storage_servers[ss_id[i]].port, client_req_id) < 0)
//                 {
//                     // failed to copy
//                     continue;
//                 }

//                 // now, we need to send from src_file->ss_ids[j] to ss_id[i]  // I am telling src_file->ss_ids[j] to copy src_file into ss_id[i] with destfilepath
//                 // send to src_file->ss_ids[j]: (content)
//                 // src_path
//                 // dest_path (folderpath/filename)
//                 // ssip ss_id[i]
//                 // port ss_id[i]

//                 num_successful++;
//                 break;
//             }

//             // close(ss_socket);
//         }
//         if (num_successful == num_chosen)
//         {
//             char *filename = (char *)strrchr(srcpath, '/');
//             if (filename[strlen(filename) - 1] == '\n')
//             {
//                 filename[strlen(filename) - 1] = '\0';
//             }
//             char *destpath = malloc(strlen(folderpath) + strlen(filename) + 1);
//             snprintf(destpath, strlen(folderpath) + strlen(filename), "%s%s", folderpath, filename);
//             destpath[strlen(folderpath) + strlen(filename)] = '\0';
//             insert_path_forc(destpath, ss_id, num_chosen, root);
//             fprintf(stderr, "File copied successfully\n");
//             if (send_success(client_socket, client_req_id, "File copied successfully\n") < 0)
//             {
//                 fprintf(stderr, "Failed to send success message to client\n");
//                 return;
//             }
//             return;
//         }
//         else
//         {
//             return send_error_response(client_socket, client_req_id, "Error: Failed to copy file to storage server\n");
//         }
//     }

//     // check for last character being "\n"
//     // check if it is a valid path
//     // check if it is a file, if it is then send copy request to the three (or how many ever) storage servers along with the ssip and port number for the ss to copy it from, along with last modified time
//     // if it is a folder, find all the files under it in the trie, then send copy request for each of the files. for each of them insert an entry in the trie, once it is successfully copied to all three backup storage servers
// }

// FOR LIST

// void handle_list_request(int client_socket, int client_req_id, char *content, long content_length)
// {
//     // Ensure the content is null-terminated
//     char *folder_path = malloc(content_length + 1);
//     memcpy(folder_path, "\0", content_length + 1);
//     if (!folder_path)
//     {
//         send_error_response(client_socket, client_req_id, "Error: Memory allocation failed\n");
//         return;
//     }
//     memcpy(folder_path, content, content_length);
//     folder_path[content_length] = '\0';
//     // Find the folder node in the trie
//     TrieNode *folder_node = search_trie_node(folder_path, root);
//     if (folder_node == NULL)
//     {
//         send_error_response(client_socket, client_req_id, "Error: Folder does not exist\n");
//         free(folder_path);
//         return;
//     }

//     // Collect all paths under the folder
//     char **paths = NULL;
//     size_t num_paths = 0;
//     list_paths(folder_node, folder_path, &paths, &num_paths);

//     // Prepare the response content
//     size_t response_content_length = 0;
//     for (size_t i = 0; i < num_paths; i++)
//     {
//         response_content_length += strlen(paths[i]) + 1; // +1 for newline
//     }

//     char *response_content = malloc(response_content_length + 1); // +1 for null-terminator
//     if (!response_content)
//     {
//         send_error_response(client_socket, client_req_id, "Error: Memory allocation failed\n");
//         free(folder_path);
//         for (size_t i = 0; i < num_paths; i++)
//             free(paths[i]);
//         free(paths);
//         return;
//     }

//     response_content[0] = '\0';
//     for (size_t i = 0; i < num_paths; i++)
//     {
//         strcat(response_content, paths[i]);
//         strcat(response_content, "\n");
//         free(paths[i]);
//     }
//     free(paths);

//     // Prepare the response header
//     char header[31];
//     char req_id_str[10];
//     char content_length_str[21];
//     snprintf(req_id_str, sizeof(req_id_str), "%09d", client_req_id);
//     snprintf(content_length_str, sizeof(content_length_str), "%020zu", response_content_length);

//     header[0] = '0'; // '0' indicates success
//     memcpy(&header[1], req_id_str, 9);
//     memcpy(&header[10], content_length_str, 20);
//     header[30] = '\0';

//     // Send the response header and content to the client
//     if (write_n_bytes(client_socket, header, 30) != 30 ||
//         write_n_bytes(client_socket, response_content, response_content_length) != (ssize_t)response_content_length)
//     {
//         fprintf(stderr, "Failed to send response to client\n");
//     }

//     free(response_content);
//     free(folder_path);
// }

void collect_paths(TrieNode *node, char *current_path, int depth, char ***output, size_t *output_length)
{
    if (node == NULL)
        return;

    // Null-terminate the current path
    current_path[depth] = '\0';

    // If the node represents a file or directory, add it to the output
    if (node->file_entry)
    {
        // Allocate or reallocate the output array
        *output = realloc(*output, (*output_length + 1) * sizeof(char *));
        if (*output == NULL)
        {
            fprintf(stderr, "Memory allocation failed\n");
            return;
        }

        (*output)[*output_length] = strdup(current_path);
        (*output_length)++;
    }

    // Recursively traverse the children
    if(node->deleted == 0){
        for (int i = 0; i < 256; i++)
        {
            if (node->children[i])
            {
                if (depth + 1 >= MAX_PATH_LENGTH)
                {
                    fprintf(stderr, "Path too long\n");
                    return;
                }

                // Append the character to the current path
                current_path[depth] = (char)i;

                // Recurse into the child node
                collect_paths(node->children[i], current_path, depth + 1, output, output_length);
            }
        }
    }
}

// Recursive function to collect all paths under a folder node
void list_paths(TrieNode *node, const char *base_path, char ***output, size_t *output_length)
{
    char current_path[MAX_PATH_LENGTH];

    size_t base_path_len = strlen(base_path);
    if (base_path_len >= MAX_PATH_LENGTH)
    {
        fprintf(stderr, "Base path too long\n");
        return;
    }

    // Copy the base path into current_path
    memcpy(current_path, base_path, base_path_len);
    current_path[base_path_len] = '\0'; // Null-terminate the base path

    *output_length = 0;
    *output = NULL;

    collect_paths(node, current_path, base_path_len, output, output_length);
}

// END FOR LIST

// Register a Storage Server
int register_storage_server(const char *ip, int port_c, int port_ns)
{
    // pthread_mutex_lock(&storage_server_mutex);
    int id = storage_server_count++;
    // pthread_mutex_unlock(&storage_server_mutex);
    storage_servers[id].id = id;
    strcpy(storage_servers[id].ip_address, ip);
    storage_servers[id].port = port_ns;
    storage_servers[id].client_port = port_c;
    storage_servers[id].file_count = 0;
    printf("Registered Storage Server %d: %s:%d\nListening for clients on port %d\n", id, ip, port_ns, port_c);
    return id;
}

void handle_storage_server(int client_socket, uint64_t id, int port, char *paths)
{
    fprintf(stderr, "Handling storage server with ID: %ld\n", id);

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
        if (path && timestamp)
        {
            // Insert the path into the trie
            int chosen_servers[3];
            int num_chosen = 0;
            choose_least_full_servers(chosen_servers, &num_chosen);
            fprintf(stderr, "Chosen servers: ");
            for (int i = 0; i < num_chosen; i++)
            {
                fprintf(stderr, "%d ", chosen_servers[i]);
            }
            fprintf(stderr, "\n");
            FileEntry *file = insert_path_forss(path, chosen_servers, num_chosen, root);
            if (file)
            {
                file->is_folder = 0;
            }

            // Set the timestamp for the file entry
            // Assuming you have a function to set the timestamp
            set_file_entry_timestamp(file, timestamp);
            fprintf(stderr, "Inserted path: %s with timestamp %s\n", path, file->last_modified);
            for (int i = 0; i < num_chosen; i++)
            {
                int ssid = chosen_servers[i];
                if (ssid == ss_id)
                    continue;
                StorageServerInfo ss_info = storage_servers[ssid];
                struct sockaddr_in storage_server_addr;
                storage_server_addr.sin_family = AF_INET;
                storage_server_addr.sin_port = htons(ss_info.client_port);
                storage_server_addr.sin_addr.s_addr = inet_addr(ss_info.ip_address);
                int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
                if (ss_socket < 0)
                {
                    fprintf(stderr, "Failed to connect to storage server %d\n", ssid);
                    continue;
                }
                if(connect(ss_socket, (struct sockaddr *)&storage_server_addr, sizeof(storage_server_addr)) < 0)
                {
                    fprintf(stderr, "Failed to connect to storage server %d\n", ssid);
                    continue;
                }
                // send 30 bytes header: 1 byte operation type, 9 bytes request id, 20 bytes content length
                // generate new content length that is length of filepath + new line + timestamp size  using strncpy
                char header[31]; // 30 bytes + null terminator
                memset(header, 0, sizeof(header));
                char req_id_str[10];
                memset(req_id_str, 0, sizeof(req_id_str));
                char content_length_str[21];
                memset(content_length_str, 0, sizeof(content_length_str));
                // content should be the path and the current timestamp in the format path timestamp
                char content[4096];
                // generate timestamp
                time_t t = time(NULL);
                struct tm tm = *localtime(&t);
                char timestamp[20];
                memset(timestamp, 0, sizeof(timestamp));
                strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm);
                snprintf(content, sizeof(content), "%s\n%s", path, timestamp);
                // add content length to content length string as character
                snprintf(content_length_str, sizeof(content_length_str), "%ld", strlen(content));
                fprintf(stderr, "content_length_str: %s\n", content_length_str);
                header[0] = '6'; // '6' for CREATE
                // set request id string to client_req_id
                snprintf(req_id_str, sizeof(req_id_str), "%09d", global_req_id);
                fprintf(stderr, "req_id_str: %s\n", req_id_str);
                // set header to success byte, request id and content length
                strncpy(&header[1], req_id_str, strlen(req_id_str));
                strncpy(&header[10], content_length_str, strlen(content_length_str));
                header[30] = '\n';
                fprintf(stderr, "header: %s\n", header);
                // send header to storage server
                if (write_n_bytes(ss_socket, header, 30) != 30)
                {
                    fprintf(stderr, "Failed to send response to client\n");
                    return;
                }
            }
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

char *requeststrings[] = {"read", "write", "stream", "info", "list", "create", "copy", "delete", "sync", "hello", "created"};

void *handle_connection(void *arg)
{
    int client_socket = *(int *)arg;
    free(arg);

    char request_type;
    ssize_t bytes_received;

    // Read REQUEST_TYPE (1 byte)
    // bytes_received = read_n_bytes(client_socket, &request_type, 1);
    request_header* header = malloc(sizeof(request_header));
    int recv_request_header = read_request_header(client_socket, header);
    request_to_string(header);
    if (recv_request_header != 0)
    {
        fprintf(stderr, "Failed to read REQUEST HEADER\n");
        // close(client_socket);
        return NULL;
    }

    enum request_type req_type = header->type;

    // Handle REQUEST_TYPE
    if (req_type == HELLO_FROM_SS) // Storage server connection
    {
        fprintf(stderr, "Received HELLO from storage server\n");

        uint64_t id = header->id;
        uint64_t content_length = header->contentLength;
        uint16_t client_port = header->port[0];

        // Allocate buffer for data
        char *data_buffer = malloc(content_length + 1);
        if (data_buffer == NULL)
        {
            fprintf(stderr, "Failed to allocate memory for data buffer\n");
            return NULL;
        }

        // Read DATA (CONTENT_LENGTH bytes) in a loop
        size_t total_bytes_read = 0;

        while (total_bytes_read < content_length)
        {
            bytes_received = recv(client_socket, data_buffer + total_bytes_read, content_length - total_bytes_read, 0);
            if (bytes_received <= 0)
            {
                fprintf(stderr, "Failed to read data\n");
                free(data_buffer);
                // close(client_socket);
                return NULL;
            }
            total_bytes_read += bytes_received;
        }

        if (data_buffer[content_length - 1] == '\0')
        {
            fprintf(stderr, "null character because");
        }
        // data_buffer[content_len-1] = '\0'; // Null-terminate the data
        data_buffer[content_length] = '\0'; // Null-terminate the data
        // fprintf(stderr, "Received data: %s\n", data_buffer);
        for (int i = 0; i < content_length; i++)
        {
            fprintf(stderr, "%c", data_buffer[i]);
        }
        fprintf(stderr, "\n");

        // Process the DATA
        // First 5 bytes are the port number
        // char port_str[6];
        // memcpy(port_str, data_buffer, 5);
        // port_str[5] = '\0'; // Null-terminate the port string
        // int port = atoi(port_str);

        // Remaining data contains strings (paths)
        // char *remaining_data = data_buffer + 5;
        // size_t remaining_length = content_len - 5;
        fprintf(stderr, "Remaining data: ");
        for (int i = 0; i < sizeof(data_buffer); i++)
        {
            if (data_buffer[i] == '\0')
            {
                // fprintf(stderr,"null character");
                data_buffer[i] = ' ';
                break;
            }
            fprintf(stderr, "%c", data_buffer[i]);
        }
        fprintf(stderr, "\n");

        // Accumulate the required strings (skip every second string)
        char accumulated_paths[16384]; // Adjust size as needed
        size_t accumulated_length = 0;

        char *line;
        char *saveptr1;
        line = strtok_r(data_buffer, "\n", &saveptr1);
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
                    // close(client_socket);
                    return NULL;
                }
            }

            line = strtok_r(NULL, "\n", &saveptr1);
        }
        accumulated_paths[accumulated_length] = '\0'; // Null-terminate the accumulated paths

        // Call handle_storage_server with the port number and accumulated paths
        handle_storage_server(client_socket, id, client_port, accumulated_paths);
        fprintf(stderr, "Finished handling storage server\n");

        free(data_buffer);

        // Continue to listen to the storage server if needed
        while (1)
        {
            response_header* response_header = malloc(sizeof(response_header));
            int recv_response_header = read_response_header(client_socket, response_header);
            uint64_t request_id = response_header->requestID;
            u_int64_t content_length = response_header->contentLength;
            enum exit_status status = response_header->status;
            fprintf(stderr, "Received response header from storage server: request_id: %ld, content_length: %ld, status: %d\n", request_id, content_length, status);

            // recieve header of 30 bytes with 1 byte request type, 9 bytes request id, 20 bytes content length for acknowledgment
            char header[31];
            bytes_received = read_n_bytes(client_socket, header, 30);
            if (bytes_received != 30)
            {
                fprintf(stderr, "Failed to read header from storage server\n");
                // close(client_socket);
                return NULL;
            }
            header[30] = '\0';
            // Parse header fields
            // char request_type;
            // char req_id_str[10];
            // char content_length_str[21];

            // request_type = header[0];
            // strncpy(req_id_str, &header[1], 9);
            // req_id_str[9] = '\0';
            // int requestID = atoi(req_id_str);
            // strncpy(content_length_str, &header[10], 20);
            // content_length_str[20] = '\0';
            // int content_length = atoi(content_length_str);
            // fprintf(stderr, "Received request from storage server\n");
            // fprintf(stderr, "request type: %c\n", request_type);
            // fprintf(stderr, "request id: %s\n", req_id_str);
            // fprintf(stderr, "content length: %d\n", content_length);

            // read content from storage server
            char *content = malloc(content_length + 1);
            if (!content)
            {
                fprintf(stderr, "Failed to allocate memory for content\n");
                // close(client_socket);
                return NULL;
            }
            bytes_received = read_n_bytes(client_socket, content, content_length);
            if (bytes_received != content_length)
            {
                fprintf(stderr, "Failed to read content from storage server\n");
                free(content);
                // close(client_socket);
                return NULL;
            }
            content[content_length] = '\0';
            fprintf(stderr, "content: %s\n", content);

            int client_fd = request_array[request_id].client_socket;
            if (client_fd < 0)
            {
                fprintf(stderr, "Invalid client socket for request ID %ld\n", request_id);
                free(content);
                continue;
            }

            respond(client_fd,-1,status,request_id,content_length,NULL,0);

            send(client_fd, header, 30, 0);
        }
    }
    else
    {
        // Handle client connections

        // fprintf(stderr, "Received request from client\n");
        fprintf(stderr, "request type: %c\n", request_type);
        handle_client(client_socket, header);
        // close(client_socket);
    }

    return NULL;
}

void handle_client(int client_socket, request_header* header)
{
    int flag = 0;
    while (1)
    {
        request_header* header = malloc(sizeof(request_header));
        // We already received the first byte of the header as initial_request_type

        char request_type = header->type;
        fprintf(stderr, "request_type: %c\n", request_type);

    
        uint64_t client_req_id = header->id;
        fprintf(stderr, "client_req_id: %ld\n", client_req_id);

        char* paths[2] = {NULL, NULL};
        if(header->paths[0][0] != '\0')
        {
            fprintf(stderr, "path 0: %s\n", header->paths[0]);
            paths[0] = header->paths[0];
        }
        if(header->paths[1][0] != '\0')
        {
            fprintf(stderr, "path 1: %s\n", header->paths[1]);
            paths[1] = header->paths[1];
        }
      
        
        pthread_mutex_lock(&global_req_id_mutex);
        int storage_req_id = global_req_id++;
        // FILE *fd = fopen("requests.txt", "a");
        // if (fd == NULL)
        // {
        //     perror("Error opening requests.txt");
        //     pthread_mutex_unlock(&global_req_id_mutex);
        //     return;
        // }

        // char file_write_buffer[256];
        // int len = snprintf(file_write_buffer, sizeof(file_write_buffer), "%d request: Op_type: %s req_id: %s content_length %s\n", storage_req_id, requeststrings[header[0] - '0' - 1], id_str, content_length_str);
        // fwrite(file_write_buffer, 1, len, fd);
        // fclose(fd);
        pthread_mutex_unlock(&global_req_id_mutex);

        client_req_id = storage_req_id;
        request_array[client_req_id].client_socket = client_socket;

        // char *saveptr;

        // char* content_copy = strdup(content);
        // int len_copy = strlen(content);
        // fprintf(stderr, "content_copy: %s\n", content_copy);
        // content = strtok_r(content, "\n", &saveptr);
        // fprintf(stderr, "tokenised content: %s\n", content);
        // content_length = strlen(content);
        // fprintf(stderr, "content_length: %ld\n", content_length);

        // Handle the request based on request_type
        if (request_type == CREATE) // '6' for CREATE
        {
            // fprintf(stderr, "Received CREATE request from client\n");
            // handle_create_request(client_socket, client_req_id, content, content_length);
        }
        else if (request_type == READ || request_type == STREAM || request_type == INFO)
        {
            if (request_type == READ)
                fprintf(stderr, "Received READ request from client\n");
            if (request_type == STREAM)
                fprintf(stderr, "Received STREAM request from client\n");
            if (request_type == INFO)
                fprintf(stderr, "Received INFO request from client\n");
            // content = strtok_r(content, "\n", &saveptr);
            // fprintf(stderr, "tokenised content: %s\n", content);
            // content_length = strlen(content);
            // fprintf(stderr, "content_length: %ld\n", content_length);
            char* path = header->paths[0];
            fprintf(stderr, "path in request: %s\n", path);
            handle_rsi_request(client_socket, client_req_id, path, request_type);
        }
        else if (request_type == '2')
        {
            fprintf(stderr, "Received WRITE request from client\n");
            // content = strtok_r(content, "\n", &saveptr);
            // fprintf(stderr, "tokenised content: %s\n", content);
            // content_length = strlen(content);
            // fprintf(stderr, "content_length: %ld\n", content_length);
            // handle_write_request(client_socket, client_req_id, content, content_length);
        }
        else if (request_type == '5')
        {
            fprintf(stderr, "Received LIST request from client\n");
            // content = strtok_r(content, "\n", &saveptr);
            // fprintf(stderr, "tokenised content: %s\n", content);
            // content_length = strlen(content);
            // fprintf(stderr, "content_length: %ld\n", content_length);
            // handle_list_request(client_socket, client_req_id, content, content_length);
        }
        else if (request_type == '7')
        {
            fprintf(stderr, "Received COPY request from client\n");
            // handle_copy_request(client_socket, client_req_id, content, content_length);
        }
        else if (request_type == '8')
        {
            fprintf(stderr, "Received DELETE request from client\n");
            // handle_delete_request(client_socket, client_req_id, content, content_length);
        }
        else
        {
            fprintf(stderr, "Invalid request type received: %c\n", request_type);
            send_error_response(client_socket, client_req_id, E_INVALID_REQUEST);
        }
        // free(content);
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
        fprintf(stderr, "new connection accepted\n");
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
    root->file_entry->is_folder = 1;

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
