#include <stdbool.h>

#ifndef STORAGESERVER_H
#define STORAGESERVER_H

extern int nm_sockfd;

#define BACKLOG 100
#define MAXPATHLENGTH 4096
#define MAXFILENAMELENGTH 256

void* handle_client(void* arg);
void ss_read(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ss_write(int fd, char* vpath, int contentLength, int requestID, char* tbf, int rcl);
struct file* ss_create(int fd, char* vpath, char* mtime, int requestID, int contentLength, char* tbf, int rcl);
void ss_delete(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ss_stream(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ss_copy(int fd, char* srcpath, int requestID, char* tbf, int rcl);
void ss_info(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ss_sync(int fd, char* vpath, int requestID, char* tbf, int rcl);

#endif