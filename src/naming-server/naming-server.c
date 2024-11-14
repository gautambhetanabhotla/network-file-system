#include "naming-server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
int ss_count = 0;
pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;

// LRU Cache for recent path lookups
typedef struct CacheNode {
    char path[MAX_PATH_LENGTH];
    StorageServerInfo *ss_info;
    struct CacheNode *prev;
    struct CacheNode *next;
} CacheNode;

CacheNode *cache_head = NULL;
CacheNode *cache_tail = NULL;
int cache_size = 0;
pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

// Logging Function
void log_message(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

// Send Error Message to Client
void send_error(int socket_fd, ErrorCode code, const char *message) {
    char buffer[MAX_MESSAGE_SIZE];
    snprintf(buffer, sizeof(buffer), "ERROR\nCODE:%d\nMESSAGE:%s\nEND\n", code, message);
    send(socket_fd, buffer, strlen(buffer), 0);
}

// Send Acknowledgment to Client
void send_ack(int socket_fd, const char *message) {
    char buffer[MAX_MESSAGE_SIZE];
    snprintf(buffer, sizeof(buffer), "ACK\n%s\nEND\n", message);
    send(socket_fd, buffer, strlen(buffer), 0);
}

// Update LRU Cache
void update_cache(const char *path, StorageServerInfo *ss_info) {
    pthread_mutex_lock(&cache_mutex);
    // Check if path is already in cache
    CacheNode *current = cache_head;
    while (current) {
        if (strcmp(current->path, path) == 0) {
            // Move to front
            if (current != cache_head) {
                // Remove from current position
                if (current->prev)
                    current->prev->next = current->next;
                if (current->next)
                    current->next->prev = current->prev;
                else
                    cache_tail = current->prev;
                // Insert at head
                current->next = cache_head;
                current->prev = NULL;
                cache_head->prev = current;
                cache_head = current;
            }
            pthread_mutex_unlock(&cache_mutex);
            return;
        }
        current = current->next;
    }
    // Add new node to cache
    CacheNode *new_node = (CacheNode *)malloc(sizeof(CacheNode));
    strcpy(new_node->path, path);
    new_node->ss_info = ss_info;
    new_node->prev = NULL;
    new_node->next = cache_head;
    if (cache_head)
        cache_head->prev = new_node;
    cache_head = new_node;
    if (!cache_tail)
        cache_tail = new_node;
    cache_size++;
    // Remove least recently used if cache is full
    if (cache_size > CACHE_SIZE) {
        CacheNode *to_remove = cache_tail;
        cache_tail = cache_tail->prev;
        if (cache_tail)
            cache_tail->next = NULL;
        free(to_remove);
        cache_size--;
    }
    pthread_mutex_unlock(&cache_mutex);
}

// Find Storage Server for Given Path
StorageServerInfo *find_storage_server_for_path(const char *path) {
    // Check Cache First
    pthread_mutex_lock(&cache_mutex);
    CacheNode *current = cache_head;
    while (current) {
        if (strcmp(current->path, path) == 0) {
            StorageServerInfo *ss_info = current->ss_info;
            // Move to front
            if (current != cache_head) {
                if (current->prev)
                    current->prev->next = current->next;
                if (current->next)
                    current->next->prev = current->prev;
                else
                    cache_tail = current->prev;
                current->next = cache_head;
                current->prev = NULL;
                cache_head->prev = current;
                cache_head = current;
            }
            pthread_mutex_unlock(&cache_mutex);
            return ss_info;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&cache_mutex);
    // Search Registry
    pthread_mutex_lock(&registry_mutex);
    for (int i = 0; i < ss_count; i++) {
        StorageServerInfo *ss_info = &storage_servers[i];
        pthread_mutex_lock(&ss_info->ss_mutex);
        if (ss_info->is_active) {
            for (int j = 0; j < ss_info->path_count; j++) {
                if (strcmp(ss_info->paths[j], path) == 0) {
                    pthread_mutex_unlock(&ss_info->ss_mutex);
                    pthread_mutex_unlock(&registry_mutex);
                    update_cache(path, ss_info);
                    return ss_info;
                }
            }
        }
        pthread_mutex_unlock(&ss_info->ss_mutex);
    }
    pthread_mutex_unlock(&registry_mutex);
    return NULL;
}

// Handle Storage Server Registration
void *storage_server_handler(void *arg) {
    int ss_socket = *(int *)arg;
    free(arg);
    char buffer[MAX_MESSAGE_SIZE];
    int bytes_received = recv(ss_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        close(ss_socket);
        pthread_exit(NULL);
    }
    buffer[bytes_received] = '\0';
    // Parse Registration Message
    StorageServerInfo ss_info;
    memset(&ss_info, 0, sizeof(ss_info));
    pthread_mutex_init(&ss_info.ss_mutex, NULL);
    ss_info.is_active = 1;
    char *token = strtok(buffer, "\n");
    while (token) {
        if (strncmp(token, "NM_PORT:", 8) == 0) {
            ss_info.nm_port = atoi(token + 8);
        } else if (strncmp(token, "CLIENT_PORT:", 12) == 0) {
            ss_info.client_port = atoi(token + 12);
        } else if (strncmp(token, "PATHS:", 6) == 0) {
            // Parse Paths
            char *paths_str = token + 6;
            char *path_token = strtok(paths_str, ",");
            while (path_token && ss_info.path_count < MAX_PATHS_PER_SS) {
                strcpy(ss_info.paths[ss_info.path_count++], path_token);
                path_token = strtok(NULL, ",");
            }
        } else if (strcmp(token, "END") == 0) {
            break;
        }
        token = strtok(NULL, "\n");
    }
    // Get IP Address from Socket
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    getpeername(ss_socket, (struct sockaddr *)&addr, &addr_size);
    strcpy(ss_info.ip_address, inet_ntoa(addr.sin_addr));
    // Update Registry
    pthread_mutex_lock(&registry_mutex);
    storage_servers[ss_count++] = ss_info;
    pthread_mutex_unlock(&registry_mutex);
    // Send ACK
    send_ack(ss_socket, "Registration Successful");
    close(ss_socket);
    log_message("Storage Server %s registered with %d paths.\n", ss_info.ip_address, ss_info.path_count);
    pthread_exit(NULL);
}

// Handle Client Requests
void *client_handler(void *arg) {
    ClientRequest *client_req = (ClientRequest *)arg;
    int client_socket = client_req->socket_fd;
    free(client_req);
    char buffer[MAX_MESSAGE_SIZE];
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        pthread_exit(NULL);
    }
    buffer[bytes_received] = '\0';
    // Parse Client Request
    char operation[16];
    char path[MAX_PATH_LENGTH];
    char additional_params[MAX_MESSAGE_SIZE];
    memset(operation, 0, sizeof(operation));
    memset(path, 0, sizeof(path));
    memset(additional_params, 0, sizeof(additional_params));
    char *token = strtok(buffer, "\n");
    while (token) {
        if (strncmp(token, "OPERATION:", 10) == 0) {
            strcpy(operation, token + 10);
        } else if (strncmp(token, "PATH:", 5) == 0) {
            strcpy(path, token + 5);
        } else if (strncmp(token, "ADDITIONAL_PARAMS:", 18) == 0) {
            strcpy(additional_params, token + 18);
        } else if (strcmp(token, "END") == 0) {
            break;
        }
        token = strtok(NULL, "\n");
    }
    log_message("Received request: OPERATION=%s PATH=%s\n", operation, path);
    // Process Request
    if (strcmp(operation, "READ") == 0 || strcmp(operation, "WRITE") == 0 ||
        strcmp(operation, "INFO") == 0 || strcmp(operation, "STREAM") == 0) {
        // Lookup Path
        StorageServerInfo *ss_info = find_storage_server_for_path(path);
        if (ss_info && ss_info->is_active) {
            // Send SS Details to Client
            char response[MAX_MESSAGE_SIZE];
            snprintf(response, sizeof(response), "SS_IP:%s\nSS_PORT:%d\nEND\n", ss_info->ip_address, ss_info->client_port);
            send_ack(client_socket, response);
            log_message("Sent storage server details to client for path %s\n", path);
        } else {
            send_error(client_socket, ERR_FILE_NOT_FOUND, "File not found");
        }
    } else if (strcmp(operation, "CREATE") == 0 || strcmp(operation, "DELETE") == 0 || strcmp(operation, "COPY") == 0) {
        // Forward Command to SS
        StorageServerInfo *ss_info = find_storage_server_for_path(path);
        if (ss_info && ss_info->is_active) {
            // Connect to SS NM Port
            int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in ss_addr;
            ss_addr.sin_family = AF_INET;
            ss_addr.sin_port = htons(ss_info->nm_port);
            inet_pton(AF_INET, ss_info->ip_address, &ss_addr.sin_addr);
            if (connect(ss_socket, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0) {
                send_error(client_socket, ERR_SERVER_UNAVAILABLE, "Storage Server unavailable");
                close(ss_socket);
                close(client_socket);
                pthread_exit(NULL);
            }
            // Send Command
            char command[MAX_COMMAND_SIZE];
            snprintf(command, sizeof(command), "COMMAND\nOPERATION:%s\nPATH:%s\nADDITIONAL_PARAMS:%s\nEND\n",
                        operation, path, additional_params);
            send(ss_socket, command, strlen(command), 0);
            // Receive ACK from SS
            char ss_response[MAX_MESSAGE_SIZE];
            int ss_bytes = recv(ss_socket, ss_response, sizeof(ss_response) - 1, 0);
            if (ss_bytes > 0) {
                ss_response[ss_bytes] = '\0';
                // Forward ACK to Client
                send(client_socket, ss_response, strlen(ss_response), 0);
                log_message("Forwarded response from SS to client.\n");
            }
            close(ss_socket);
        } else {
            send_error(client_socket, ERR_FILE_NOT_FOUND, "File not found");
        }
    } else if (strcmp(operation, "LIST") == 0) {
        // Send List of Accessible Paths
        char response[MAX_MESSAGE_SIZE];
        strcpy(response, "PATHS:\n");
        pthread_mutex_lock(&registry_mutex);
        for (int i = 0; i < ss_count; i++) {
            StorageServerInfo *ss_info = &storage_servers[i];
            pthread_mutex_lock(&ss_info->ss_mutex);
            if (ss_info->is_active) {
                for (int j = 0; j < ss_info->path_count; j++) {
                    strcat(response, ss_info->paths[j]);
                    strcat(response, "\n");
                }
            }
            pthread_mutex_unlock(&ss_info->ss_mutex);
        }
        pthread_mutex_unlock(&registry_mutex);
        strcat(response, "END\n");
        send_ack(client_socket, response);
    } else {
        send_error(client_socket, ERR_INVALID_REQUEST, "Invalid operation");
    }
    close(client_socket);
    pthread_exit(NULL);
}

// Initialize Naming Server
void initialize_naming_server(int ss_reg_port, int client_req_port) {
    int ss_reg_socket, client_req_socket;
    struct sockaddr_in ss_reg_addr, client_req_addr;

    // Setup SS Registration Socket
    ss_reg_socket = socket(AF_INET, SOCK_STREAM, 0);
    ss_reg_addr.sin_family = AF_INET;
    ss_reg_addr.sin_addr.s_addr = INADDR_ANY;
    ss_reg_addr.sin_port = htons(ss_reg_port);

    bind(ss_reg_socket, (struct sockaddr *)&ss_reg_addr, sizeof(ss_reg_addr));
    listen(ss_reg_socket, 5);
    log_message("Naming Server listening for Storage Server registrations on port %d...\n", ss_reg_port);

    // Setup Client Request Socket
    client_req_socket = socket(AF_INET, SOCK_STREAM, 0);
    client_req_addr.sin_family = AF_INET;
    client_req_addr.sin_addr.s_addr = INADDR_ANY;
    client_req_addr.sin_port = htons(client_req_port);

    bind(client_req_socket, (struct sockaddr *)&client_req_addr, sizeof(client_req_addr));
    listen(client_req_socket, 5);
    log_message("Naming Server listening for client requests on port %d...\n", client_req_port);

    // Accept Connections in Separate Threads
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(ss_reg_socket, &readfds);
        FD_SET(client_req_socket, &readfds);
        int max_sd = (ss_reg_socket > client_req_socket) ? ss_reg_socket : client_req_socket;
        select(max_sd + 1, &readfds, NULL, NULL, NULL);
        // Handle SS Registration
        if (FD_ISSET(ss_reg_socket, &readfds)) {
            int new_socket = accept(ss_reg_socket, NULL, NULL);
            int *arg = malloc(sizeof(int));
            *arg = new_socket;
            pthread_t ss_thread;
            pthread_create(&ss_thread, NULL, storage_server_handler, arg);
            pthread_detach(ss_thread);
        }
        // Handle Client Requests
        if (FD_ISSET(client_req_socket, &readfds)) {
            ClientRequest *client_req = malloc(sizeof(ClientRequest));
            socklen_t addr_size = sizeof(client_req->client_addr);
            client_req->socket_fd = accept(client_req_socket, (struct sockaddr *)&client_req->client_addr, &addr_size);
            pthread_t client_thread;
            pthread_create(&client_thread, NULL, client_handler, client_req);
            pthread_detach(client_thread);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: ./naming_server <ss_registration_port> <client_request_port>\n");
        exit(EXIT_FAILURE);
    }
    int ss_reg_port = atoi(argv[1]);
    int client_req_port = atoi(argv[2]);
    initialize_naming_server(ss_reg_port, client_req_port);
    return 0;
}