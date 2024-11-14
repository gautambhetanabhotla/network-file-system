#include "storage-server.h"

#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <string.h>
#include <pthread.h>

#define BACKLOG 100
#define MAXPATHLENGTH 4096
#define MAXFILENAMELENGTH 256

/*

Important: Request exchange format

On startup - 

STORAGESERVER
<port>
<path1>
<path2>
<path3>
.....
STOP,,,

READ
<virtual path>

WRITE
<virtual path>
<content length>
<content>

*/

void* handle_client(void* arg) {
    // fprintf(stderr, "EW$RFSVSFWEFSESF");
    printf("RFVCVSFDDV");
    int client_sockfd = *(int*)arg;
    char buf[8192];
    recv(client_sockfd, buf, sizeof(buf), 0);
    char request_type[8192];
    sscanf(buf, "%s", request_type);
    if(strcmp(request_type, "READ") == 0) {
        ss_read((void*)&client_sockfd);
    }
    else if(strcmp(request_type, "WRITE") == 0) {
        ss_write((void*)&client_sockfd);
    }
    else if(strcmp(request_type, "CREATE") == 0) {
        ss_create((void*)&client_sockfd);
    }
    else if(strcmp(request_type, "DELETE") == 0) {
        ss_delete((void*)&client_sockfd);
    }
    else if(strcmp(request_type, "STREAM") == 0) {
        ss_stream((void*)&client_sockfd);
    }
    else {
        
    }
    return NULL;
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

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <IP Address> <Port>\n", argv[0]);
        exit(1);
    }

    // Listen for clients
    int ss_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(ss_sockfd < 0) {
        perror("Socket couldn't be created");
        exit(1);
    }
    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    int PORT = 3000;
    listen_addr.sin_port = htons(PORT);
    while(bind(ss_sockfd, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) < 0 && PORT < 65000) {
        // Error handling pending
        PORT++;
        listen_addr.sin_port = htons(PORT);
    }
    if(listen(ss_sockfd, BACKLOG) < 0) {
        perror("Listening on port failed");
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

    send(nm_sockfd, "STORAGESERVER\n", strlen("STORAGESERVER\n"), 0);

    // Send the port you're using to listen for clients
    char port_str[15];
    snprintf(port_str, 13, "%d\n", PORT);
    send(nm_sockfd, port_str, strlen(port_str), 0);

    // Send the list of accessible paths
    FILE* pathsfile = fopen("./paths.txt", "r");
    if(pathsfile == NULL) pathsfile = fopen("./paths.txt", "w");
    fclose(pathsfile);
    pathsfile = fopen("./paths.txt", "r");
    while(!feof(pathsfile)) {
        char vpath[MAXPATHLENGTH], rpath[MAXPATHLENGTH];
        fscanf(pathsfile, "%s", vpath);
        if(strlen(vpath) == 0) break;
        if(feof(pathsfile)) {
            fprintf(stderr, "Invalid paths file!\n");
            exit(1);
        }
        fscanf(pathsfile, "%s", rpath);
        if(strlen(rpath) == 0) {
            fprintf(stderr, "Invalid paths file!\n");
            exit(1);
        }
        add_file_entry(vpath, rpath);
        send(nm_sockfd, vpath, strlen(vpath), 0);
    }
    // End with "STOP,,," (commas cause file paths wont contain commas)
    send(nm_sockfd, "STOP,,,\n", strlen("STOP,,,\n"), 0);

    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        pthread_t client_thread;
        int client_fd = accept(ss_sockfd, (struct sockaddr *)&client_addr, &client_len);
        pthread_create(&client_thread, NULL, handle_client, (void *)&client_fd);
        // pthread_detach(client_thread);
    }
    
    close(nm_sockfd);
    close(ss_sockfd);
    return 0;
}