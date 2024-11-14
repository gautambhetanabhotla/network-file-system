#ifndef NAMING_SERVER_H
#define NAMING_SERVER_H

#include <netinet/in.h>
#include <pthread.h>

// Maximum sizes
#define MAX_STORAGE_SERVERS 100
#define MAX_CLIENTS 100
#define MAX_PATH_LENGTH 256
#define MAX_PATHS_PER_SS 1000
#define MAX_MESSAGE_SIZE 1024
#define CACHE_SIZE 100
#define MAX_COMMAND_SIZE 2048

// Error Codes
typedef enum {
    ERR_NONE,
    ERR_FILE_NOT_FOUND,
    ERR_FILE_LOCKED,
    ERR_PERMISSION_DENIED,
    ERR_INVALID_REQUEST,
    ERR_SERVER_UNAVAILABLE
} ErrorCode;

// Storage Server Information
typedef struct {
    char ip_address[INET_ADDRSTRLEN];
    int nm_port;       // Port for NM communication
    int client_port;   // Port for Client communication
    char paths[MAX_PATHS_PER_SS][MAX_PATH_LENGTH];
    int path_count;
    int is_active;
    pthread_mutex_t ss_mutex;
} StorageServerInfo;

// Client Request Structure
typedef struct {
    int socket_fd;
    struct sockaddr_in client_addr;
} ClientRequest;

// Function Declarations
void *storage_server_handler(void *arg);
void *client_handler(void *arg);
void initialize_naming_server(int ss_reg_port, int client_req_port);
void log_message(const char *format, ...);
StorageServerInfo *find_storage_server_for_path(const char *path);
void update_registry(StorageServerInfo *ss_info);
void send_error(int socket_fd, ErrorCode code, const char *message);
void send_ack(int socket_fd, const char *message);

#endif // NAMING_SERVER_H