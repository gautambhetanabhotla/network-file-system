#include "../src/storage-server/requests.h"

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
    char req[11] = {0}, data[8192] = {0};
    // scanf("%s", req);
    strcpy(req, "create");
    strcpy(data, "/hsa/sfh\n2222-22-22T22:22:22");
    int i = 0;
    for(i = 0; i < sizeof(requeststrings)/sizeof(char*); i++) if(strcmp(req, requeststrings[i]) == 0) break;
    // while(!feof(stdin)) fgets(data, 8192, stdin);
    // scanf("%s%s%s", req, vpath, mtime);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(argv[1]);
    addr.sin_port = htons(atoi(argv[2]));
    int ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));

    char header[11] = {'\0'}; header[0] = '0' + i + 1;
    snprintf(header + 1, 9, "%d", 0);
    char CL[21]; CL[20] = '\0';
    sprintf(CL, "%ld", strlen(data));

    send(fd, header, sizeof(header) - 1, 0);
    send(fd, CL, sizeof(CL) - 1, 0);
    send(fd, data, strlen(data), 0);

    char buf[8192] = {0};
    recv(fd, buf, 30, 0);
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