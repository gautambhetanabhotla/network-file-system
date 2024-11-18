#include <stdbool.h>

#ifndef REQUESTS_H
#define REQUESTS_H

#define BACKLOG 100
#define MAXPATHLENGTH 4096
#define MAXFILENAMELENGTH 256

void* handle_client(void* arg);
void ss_read(int fd, char* vpath, int requestID);
void ss_write(int fd, char* vpath, int contentLength, int requestID);
struct file* ss_create(int fd, char* vpath, char* mtime, int requestID);
void ss_delete(int fd, char* vpath, int requestID);
void ss_stream(int fd, char* vpath, int requestID);
void ss_copy(int fd, char* vpath, char* vpath2, int requestID);
void ss_info(int fd, char* vpath, int requestID);
void ss_update_mtime(int fd, char* vpath, char* mtime, int requestID);
void ns_synchronize(int fd, char* vpath, int requestID);

#endif