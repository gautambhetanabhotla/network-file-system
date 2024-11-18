#include <stdbool.h>

#ifndef REQUESTS_H
#define REQUESTS_H

extern int nm_sockfd;

#define BACKLOG 100
#define MAXPATHLENGTH 4096
#define MAXFILENAMELENGTH 256

enum request_type {
    READ = 1, WRITE, STREAM, INFO, LIST, CREATE, COPY, DELETE, SYNC, HELLO, CREATED
};

enum exit_status {
    SUCCESS, ACK, E_FILE_DOESNT_EXIST, E_INCOMPLETE_WRITE, E_FILE_ALREADY_EXISTS, E_WRONG_SS, E_FAULTY_SS
};

int recv_full(int fd, char* buf, int contentLength);

void* handle_client(void* arg);
void ss_read(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ss_write(int fd, char* vpath, int contentLength, int requestID, char* tbf, int rcl);
struct file* ss_create(int fd, char* vpath, char* mtime, int requestID, int contentLength, char* tbf, int rcl);
void ss_delete(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ss_stream(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ss_copy(int fd, char* vpath, char* vpath2, int requestID, char* tbf, int rcl);
void ss_info(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ns_synchronize(int fd, char* vpath, int requestID);

void request(int clfd, int nmfd, enum request_type type, long contentLength);
void respond(int clfd, int nmfd, enum exit_status status, int requestID, long contentLength);

#endif