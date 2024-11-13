#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

void *sender() {

}

void* listener() {

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
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
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

    if(connect(sockfd, (struct sockaddr*)&nm_server_addr, sizeof(nm_server_addr)) < 0) {
        perror("Connection failed");
        exit(1);
    }

    while(1) {
        char buf[4096];
        printf("Enter the file name: ");
        scanf("%s", buf);
        write(sockfd, buf, sizeof(buf));
    }
}