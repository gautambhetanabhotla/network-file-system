#include "../src/storage-server/storageserver.h"
#include "../src/lib/request.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

char* requeststrings[] = {"read", "write", "stream", "info", "list", "create", "copy", "delete", "sync", "hello", "created"};

// enum exit_status {
//     SUCCESS, ACK, E_FILE_DOESNT_EXIST, E_INCOMPLETE_WRITE, E_FILE_ALREADY_EXISTS, E_WRONG_SS, E_FAULTY_SS
// };

// extern int nm_sockfd;

int main(int argc, char* argv[]) {
    int sockfd = connect_with_ip_port(argv[0], atoi(argv[1]));
    int ports = {2123, 0};
    request(sockfd, -1, HELLO_FROM_SS, 0, NULL, NULL, ports);
    
    // printf("%s\n", buf);
    switch(buf[0] - '0') {
        case SUCCESS: printf("Success\n"); break;
        case ACK: printf("Ack\n"); break;
        case E_FILE_DOESNT_EXIST: printf("File doesn't exist\n"); break;
        case E_INCOMPLETE_WRITE: printf("Incomplete write\n"); break;
        case E_FILE_ALREADY_EXISTS: printf("File already exists\n"); break;
        case E_WRONG_SS: printf("Wrong storage server\n"); break;
        case E_FAULTY_SS: printf("Faulty storage server\n"); break;
    }
    int cl = atoi(buf + 10);
    recv(fd, buf, cl, 0);
    printf("%s\n", buf);
    close(fd);
    return 0;
}