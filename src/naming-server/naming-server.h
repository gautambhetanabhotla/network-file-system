#ifndef NAMING_SERVER_H
#define NAMING_SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <semaphore.h>
#include <netinet/in.h>
#include <pthread.h>

// Maximum sizes
#define MAX_STORAGE_SERVERS 10
#define MAX_CLIENTS 100
#define MAX_PATH_LENGTH 1024
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
    int port;       // Port for NM communication
    int client_port;   // Port for Client communication
    char paths[MAX_PATHS_PER_SS][MAX_PATH_LENGTH];
    int id;
    int path_count;
    int is_active;
    pthread_mutex_t ss_mutex;
} StorageServerInfo;

// Client Request Structure
typedef struct {
    int socket_fd;
    struct sockaddr_in client_addr;
} ClientRequest;

#define MAX_CLIENTS 100
#define MAX_FILENAME_LENGTH 1024
#define MAX_IP_LENGTH 16
#define PORT_LEN 6

// File Entry Structure
typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    int storage_server_id;
    int backup1_server_id;
    int backup2_server_id;
    time_t last_modified;

    struct FileEntry *is_copy;
} FileEntry;

// Trie Node Structure
typedef struct TrieNode {
    struct TrieNode *children[256];
    FileEntry *file_entry;
} TrieNode;



// Cache Node Structure for LRU Cache
typedef struct CacheNode {
    char path[MAX_PATH_LENGTH];
    int storage_server_id;
    struct CacheNode *prev;
    struct CacheNode *next;
} CacheNode;

// Function Declarations
void *storage_server_handler(void *arg);
void *client_handler(void *arg);
void initialize_naming_server(int ss_reg_port, int client_req_port);
void log_message(const char *format, ...);
StorageServerInfo *find_storage_server_for_path(const char *path);
void update_registry(StorageServerInfo *ss_info);
void send_error(int socket_fd, ErrorCode code, const char *message);
void send_ack(int socket_fd, const char *message);
TrieNode *create_trie_node();
void insert_path(const char *path, int storage_server_id);
int search_path(const char *path);
int register_storage_server(const char *ip, int port);
void handle_client(int client_socket, char *buffer);
void save_trie(const char *filename);
void load_trie(const char *filename);
void save_cache(const char *filename);
void load_cache(const char *filename);
void signal_handler(int sig);

#endif // NAMING_SERVER_H