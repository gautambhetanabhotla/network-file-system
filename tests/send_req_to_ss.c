#include "../src/storage-server/requests.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// extern int nm_sockfd;

int main(int argc, char* argv[]) {
    char req[11] = {0}, vpath[4096] = {0}, mtime[21] = {0};
    // char data[100] = "/home/gautam\nSfdsdgsdgsgfgdfdfg";
    char data[100] = "/home/gautam\n";
    // scanf("%s%s%s", req, vpath, mtime);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(argv[1]);
    addr.sin_port = htons(atoi(argv[2]));
    int ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    char header[11] = {'\0'}; header[0] = '0' + READ;
    snprintf(header + 1, 9, "%d", 0);
    char CL[21]; CL[20] = '\0';
    sprintf(CL, "%ld", strlen(data));
    send(fd, header, sizeof(header) - 1, 0);
    send(fd, CL, sizeof(CL) - 1, 0);
    send(fd, data, strlen(data), 0);
    char buf[8192] = {0};
    recv(fd, buf, 30, 0);
    printf("%s\n", buf);
    int cl = atoi(buf + 10);
    recv(fd, buf, cl, 0);
    printf("%s\n", buf);
    close(fd);
    return 0;
}