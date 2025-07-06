#include <stdbool.h>
#include "../lib/request.h"

#ifndef STORAGESERVER_H
#define STORAGESERVER_H

extern int nm_sockfd;

#define BACKLOG 100
#define MAXPATHLENGTH 4096
#define MAXFILENAMELENGTH 256

void* handle_client(void* arg);
void ss_read(request_header* req, int client_sockfd);
void ss_write(request_header* req, int client_sockfd);
struct file* ss_create(request_header* req, int client_sockfd);
void ss_delete(request_header* req, int client_sockfd);
void ss_copy(request_header* req, int client_sockfd);
void ss_info(request_header* req, int client_sockfd);
void ss_sync(request_header* req, int client_sockfd);

#endif