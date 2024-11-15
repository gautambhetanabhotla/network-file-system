#include <stdbool.h>

#ifndef REQUESTS_H
#define REQUESTS_H

#define BACKLOG 100
#define MAXPATHLENGTH 4096
#define MAXFILENAMELENGTH 256

void* handle_client(void* arg);
void ss_read(int fd, char* vpath);
void ss_write(int fd, bool sync, char* vpath, int contentLength);
void ss_create(int fd, char* vpath);
void ss_delete(int fd, char* vpath);
void ss_stream(int fd, char* vpath);

#endif