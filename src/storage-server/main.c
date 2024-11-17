#include "requests.h"
#include "files.h"

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
#include <stdbool.h>
#include <semaphore.h>

extern sem_t n_file_sem;

int nm_sockfd;

void createStorageDirectory() {
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
}

int connect_to_naming_server(int argc, char* argv[]) {
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
    return nm_sockfd;
}

void send_paths(int nm_sockfd) {
    FILE* pathsfile = fopen("./paths.txt", "r");
    if(pathsfile == NULL) pathsfile = fopen("./paths.txt", "w");
    fclose(pathsfile);
    pathsfile = fopen("./paths.txt", "r");
    fseek(pathsfile, 0, SEEK_END);
    long byte_count = ftell(pathsfile);
    fseek(pathsfile, 0, SEEK_SET);
    char CL[21]; CL[20] = '\0';
    sprintf(CL, "%ld", byte_count);
    fprintf(stderr, "SENDING CONTENT LENGTH %ld\n", byte_count);
    send(nm_sockfd, CL, sizeof(CL) - 1, 0);
    while(!feof(pathsfile)) {
        int flag = 0;
        char vpath[MAXPATHLENGTH + 1], rpath[MAXPATHLENGTH + 1];
        fscanf(pathsfile, "%s", vpath);
        while(strlen(vpath) == 0) {
            if(feof(pathsfile)) {
                flag = 1;
                break;
            }
            fscanf(pathsfile, "%s", vpath);
        }
        if(flag == 1) break;
        if(feof(pathsfile)) {
            fprintf(stderr, "Invalid paths file!\n");
            exit(1);
        }
        fscanf(pathsfile, "%s", rpath);
        while(strlen(rpath) == 0) {
            if(feof(pathsfile)) {
                fprintf(stderr, "Invalid paths file!\n");
                exit(1);
            }
            fscanf(pathsfile, "%s", rpath);
        }
        if(feof(pathsfile)) {
            fprintf(stderr, "Invalid paths file!\n");
            exit(1);
        }
        char mtime[20];
        fscanf(pathsfile, "%s", mtime);
        while(strlen(mtime) == 0) {
            if(feof(pathsfile)) {
                fprintf(stderr, "Invalid paths file!\n");
                exit(1);
            }
            fscanf(pathsfile, "%s", mtime);
        }
        add_file_entry(vpath, rpath, mtime, false);
        send(nm_sockfd, vpath, strlen(vpath), 0);
        send(nm_sockfd, " ", 1, 0);
        send(nm_sockfd, rpath, strlen(vpath), 0);
        send(nm_sockfd, " ", 1, 0);
        send(nm_sockfd, mtime, strlen(mtime), 0);
        send(nm_sockfd, "\n", 1, 0);
        vpath[0] = '\0'; rpath[0] = '\0'; mtime[0] = '\0';
    }
}

int main(int argc, char* argv[]) {

    createStorageDirectory();
    sem_init(&n_file_sem, 0, 1);

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

    // Initialise stuff with naming server
    nm_sockfd = connect_to_naming_server(argc, argv);
    send(nm_sockfd, "STORAGESERVER", strlen("STORAGESERVER"), 0);
    // Send the port you're using to listen for clients
    char port_str[6] = {'\0'};
    sprintf(port_str, "%d", PORT);
    send(nm_sockfd, port_str, sizeof(port_str) - 1, 0);
    // Send the list of accessible paths
    send_paths(nm_sockfd); 
    // send(nm_sockfd, "STOP,,,\n", strlen("STOP,,,\n"), 0);

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