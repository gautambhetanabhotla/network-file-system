#include "naming-server.h"

NamingServer* create_naming_server(int port){
    NamingServer *ns = (NamingServer*)malloc(sizeof(NamingServer));
    if(!ns){
        perror("Error in creating naming server");
        return NULL;
    }

    // initialize mutexes
    pthread_mutex_init(&ns->server_mutex, NULL);
    pthread_mutex_init(&ns->file_mutex, NULL);

    // Initialize counters
    ns->server_count = 0;
    ns->file_count = 0;

    // Create server socket
    ns->server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(ns->server_socket_fd < 0){
        perror("Error in creating server socket");
        free(ns);
        return NULL;
    }

    // set server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // bind server address to socket
    if(bind(ns->server_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Error in binding server address to socket");
        free(ns);
        return NULL;
    }

    return ns;
}

void destroy_naming_server(NamingServer *ns){
    if(!ns) return;

    // close all server sockets
    for(int i=0; i<ns->server_count; i++){
        if(ns->servers[i].socket_fd>0)
            close(ns->servers[i].socket_fd);
    }

    // close server socket
    if(ns->server_socket_fd>0)
        close(ns->server_socket_fd);
    
    // destroy mutexes
    pthread_mutex_destroy(&ns->server_mutex);
    pthread_mutex_destroy(&ns->file_mutex);

    free(ns);
}

void *handle_SS_connection(NamingServer *ns, int client_socket_fd){
    pthread_mutex_lock(&ns->server_mutex);

    //registering a new SS server
    if(ns->server_count>=MAX_SERVERS){
        perror("Max server limit reached");
        pthread_mutex_unlock(&ns->server_mutex);
        return;
    }

    StorageServer *new_server=&ns->servers[ns->server_count];

    // receive server details 
    // protocol : implement a proper protocol to receive server details
    recv(client_socket_fd, new_server->ip, 16, 0);
    recv(client_socket_fd, &new_server->naming_port, sizeof(int), 0);
    recv(client_socket_fd, &new_server->client_port, sizeof(int), 0);

    new_server->is_active = 1;
    new_server->socket_fd = client_socket_fd;
    ns->server_count++;
    
    pthread_mutex_unlock(&ns->server_mutex);
}

void *handle_client_connection(void *args){
    struct {
        NamingServer *ns;
        int client_socket;
    } *conn_info = args;

    // Buffer for receiving commands
    char buffer[1024];
    ssize_t bytes_received;

    while ((bytes_received = recv(conn_info->client_socket, buffer, sizeof(buffer), 0)) > 0) {
        // Parse command and handle accordingly
        // Implementation depends on your protocol design
    }

    close(conn_info->client_socket);
    free(conn_info);
    return NULL;
}

int find_server_for_file(NamingServer *ns, const char *path) {
    pthread_mutex_lock(&ns->file_mutex);
    
    for (int i = 0; i < ns->file_count; i++) {
        if (strcmp(ns->files[i].path, path) == 0) {
            int server_id = ns->files[i].server_id;
            pthread_mutex_unlock(&ns->file_mutex);
            return server_id;
        }
    }

    pthread_mutex_unlock(&ns->file_mutex);
    return -1;
}