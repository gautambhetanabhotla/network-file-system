#include "request.h"

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

uint64_t next_req_id = 0;
pthread_mutex_t req_id_lock = PTHREAD_MUTEX_INITIALIZER;

int get_new_request_id() {
    pthread_mutex_lock(&req_id_lock);
    uint64_t id = next_req_id++;
    pthread_mutex_unlock(&req_id_lock);
    return id;
}

void request_to_string(request_header* req) {
    char* request_type_strings[] = {
        "", "READ", "WRITE", "STREAM", "INFO", "LIST", "CREATE", "COPY", "DELETE",
        "HELLO_FROM_CLIENT", "HELLO_FROM_SS", "SYNC_BACK"
    };
    fprintf(stderr, "REQUEST %lu: %s\nContent length: %lu\n%s\n%s", req->id, request_type_strings[req->type],
            req->contentLength, req->paths[0], req->paths[1]);
}

/**
 * Sends a response with the given exit status, request ID, and content length
 * to the specified client and/or naming server file descriptors.
 *
 * @param nmfd File descriptor for the naming server. If -1, no response is sent to the naming server.
 * @param clfd File descriptor for the client. If -1, no response is sent to the client.
 * @param status The exit status code of the response.
 * @param requestID The unique identifier for the request being responded to.
 * @param contentLength The length of the content of the response.
 * @param ip The IP address of the client or server to which the response is sent. If NULL, no IP is set.
 * @param port The port number of the client or server to which the response is sent.
 */
void respond(int fd1, int fd2, enum exit_status status, int requestID, long contentLength, char* ip, uint16_t port) {
    char* exitstatusstrings[] = {"success", "acknowledge", "file doesn't exist", "incomplete write", "file already exists", "nm chose the wrong ss", "an error from SS's side", "connection refused"};
    struct response_header header = {requestID, contentLength, status};
    if(ip) strncpy(header.ip, ip, INET_ADDRSTRLEN - 1);
    header.ip[INET_ADDRSTRLEN - 1] = '\0';
    header.port = port;
    if(fd1 != -1) send(fd1, &header, sizeof(header), 0);
    if(fd2 != -1) send(fd2, &header, sizeof(header), 0);
}

/**
 * Sends a request header to the naming server and/or client with the specified type and content length.
 *
 * @param fd1 File descriptor for the naming server. If -1, no request is sent to the naming server.
 * @param fd2 File descriptor for the client. If -1, no request is sent to the client.
 * @param type The type of request being sent.
 * @param contentLength The length of the content of the request body.
 * @param paths An array of two strings representing the paths involved in the request.
 * @param ips An array of two strings representing the IP addresses involved in the request.
 * @param ports An array of two integers representing the ports involved in the request.
 */
void request(int fd1, int fd2, enum request_type type, long contentLength, char** paths, char** ips, uint16_t* ports) {
    request_header header = {get_new_request_id(), contentLength, type};
    if(paths && paths[0]) strncpy(header.paths[0], paths[0], PATH_MAX - 1);
    if(ips && ips[0]) strncpy(header.ip[0], ips[0], INET_ADDRSTRLEN - 1);
    header.ip[0][INET_ADDRSTRLEN - 1] = '\0';
    header.port[0] = ports[0];
    if(paths && paths[1]) strncpy(header.paths[1], paths[1], PATH_MAX - 1);
    if(ips && ips[1]) strncpy(header.ip[1], ips[1], INET_ADDRSTRLEN - 1);
    header.ip[1][INET_ADDRSTRLEN - 1] = '\0';
    header.port[1] = ports[1];
    header.paths[0][PATH_MAX - 1] = '\0';
    // strncpy(header.paths[1], paths[1], PATH_MAX - 1);
    header.paths[1][PATH_MAX - 1] = '\0';
    if(fd1 != -1) send(fd1, &header, sizeof(header), 0);
    if(fd2 != -1) send(fd2, &header, sizeof(header), 0);
}

int connect_with_ip_port(const char* ip, uint16_t port) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return -1;
    }
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Connection failed");
        fprintf(stderr, "Failed to connect to %s:%d\n", ip, port);
        close(sockfd);
        return -1;
    }
    return sockfd;
}
