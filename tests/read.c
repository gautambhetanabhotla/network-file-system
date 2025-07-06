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
    int sockfd = connect_with_ip_port(argv[1], atoi(argv[2]));
    uint16_t ports[2] = {2123, 0};
    char* paths[2] = {"/tp2", NULL};
    request(sockfd, -1, READ, 0, paths, NULL, ports);
    response_header resp;
    if (recv(sockfd, &resp, sizeof(resp), 0) <= 0) perror("Failed to receive response");
    int bytes_received = 0;
    while (bytes_received < resp.contentLength) {
        char buffer[8192];
        int n = recv(sockfd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            perror("Failed to receive data");
            break;
        }
        bytes_received += n;
        fwrite(buffer, 1, n, stdout); // Print the received data to stdout
    }
    close(sockfd);
    return 0;
}