#ifndef NAMING_SERVER_H
#define NAMING_SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define MAX_PATH_LENGTH 256
#define MAX_SERVERS 10
#define MAX_FILES 1000

typedef struct StorageServer {
    char ip[16];
    int naming_port;            // Port for naming server communication
    int client_port;            // Port for client communication
    char **accessible_paths;    // List of paths accessible by this server
    int num_accessible_paths;
    int is_active;              // 1 if server is active, 0 otherwise
    int socket_fd;              
}StorageServer;

typedef struct FileInfo {
    char path[MAX_PATH_LENGTH];
    int server_id;
    long size;                  // Size of file in bytes
    char type;                  // 'f' for file, 'd' for directory
} FileInfo; 

typedef struct NamingServer {
    StorageServer servers[MAX_SERVERS];
    FileInfo files[MAX_FILES];
    int server_count;
    int file_count;
    int server_socket_fd;
    pthread_mutex_t server_mutex;   // ensures that only one thread can access the server list at a time
    pthread_mutex_t file_mutex;     // ensures that only one thread can access the file list at a time
}NamingServer;

NamingServer* create_naming_server(int port);
void start_naming_server(NamingServer *ns);
void destroy_naming_server(NamingServer *ns);
void* handle_SS_connection(NamingServer *ns, int client_socket_fd);
void* handle_client_connection(void *args);          
int find_server_for_file(NamingServer *ns, const char *path);   // Returns the server_id of the server that contains the file

#endif