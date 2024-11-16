#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    char req[11] = {0}, vpath[4096] = {0}, mtime[21] = {0};
    scanf("%s%s%s", req, vpath, mtime);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(3000);
    connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    send(fd, req, 10, 0);
    send(fd, vpath, 4096, 0);
    send(fd, mtime, 21, 0);
    char buf[8192];
    recv(fd, buf, 8192, 0);
    printf("%s\n", buf);
    close(fd);
    return 0;
}