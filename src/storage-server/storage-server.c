#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>

void *sender() {

}

void* listener() {
#include <sys/stat.h>
#include <pthread.h>

#define BACKLOG 100

/*

Important: Request exchange format

On startup - 

STORAGESERVER
IP
port
path1
path2
path3
.....
STOP

READ
<virtual path>

WRITE
<virtual path>
<content length>
<content>

*/

void* handle_client(void* arg) {

}

void* handle_ns(void* arg) {

}

int main(int argc, char* argv[]) {
    DIR* storage_dir = opendir("./storage");
    if(storage_dir == NULL) {
        mkdir("./storage", 0777);
        storage_dir = opendir("./storage");
        if(storage_dir == NULL) {
            perror("Storage directory couldn't be created");
            exit(1);
        }
    }
    closedir(storage_dir);
    FILE* pathsfile = fopen("./paths.txt", "r");
    if(pathsfile == NULL) pathsfile = fopen("./paths.txt", "w");
    fclose(pathsfile);

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <IP Address> <Port>\n", argv[0]);
        exit(1);
    }
    

    int nm_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(nm_sockfd < 0) {
        perror("Socket couldn't be created");
        exit(1);
    }
    int nm_server_port = atoi(argv[2]);
    char* nm_server_ip = argv[1];
    struct sockaddr_in nm_server_addr;
    nm_server_addr.sin_family = AF_INET;
    nm_server_addr.sin_port = htons(nm_server_port);
    if(inet_pton(AF_INET, nm_server_ip, &nm_server_addr.sin_addr) <= 0) {
        perror("Invalid/unsupported address");
        exit(1);
    }
    if(connect(nm_sockfd, (struct sockaddr*)&nm_server_addr, sizeof(nm_server_addr)) < 0) {
        perror("Connection to naming server failed");
        exit(1);
    }
    pthread_t ns_thread;
    pthread_create(&ns_thread, NULL, handle_ns, (void *)&nm_sockfd);

    int listen_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_sockfd < 0) {
        perror("Socket couldn't be created");
        exit(1);
    }
    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    int PORT = 3000;
    while(bind(listen_sockfd, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) < 0 && PORT < 65000) {
        PORT++;
        listen_addr.sin_port = htons(PORT);
    }
    if(listen(listen_sockfd, BACKLOG) < 0) {
        perror("Listening on port failed");
        exit(1);
    }

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        pthread_t client_thread;
        int client_fd = accept(listen_sockfd, (struct sockaddr *)&client_addr, &client_len);
        pthread_create(&client_thread, NULL, handle_client, (void *)&client_fd);
        pthread_detach(client_thread);
    }
}