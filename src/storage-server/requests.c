#include "requests.h"
#include "files.h"

#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

extern struct file** file_entries = NULL;
extern unsigned long long int n_file_entries = 0;

/*

Important: Request exchange format

On startup - 

STORAGESERVER
<port>
<path1>
<path2>
<path3>
.....
STOP,,,

READ
<virtual path>

WRITE
<virtual path>
<content length>
<content>

COPY
<source vpath>
<dest vpath>

STREAM
<virtual path>

*/

void* handle_client(void* arg) {
    fprintf(stderr, "Client arrived!\n");
    // printf("RFVCVSFDDV");
    int client_sockfd = *(int*)arg;
    sem_t fdLock;
    sem_init(&fdLock, 0, 1);
    char buf[MAXPATHLENGTH + 102];
    if(recv(client_sockfd, buf, sizeof(buf), 0) <= 0) {
        fprintf(stderr, "Client disconnected!\n");
        return NULL;
    }
    char request_type[101], vpath[MAXPATHLENGTH + 1];
    int contentLength = 0;
    sscanf(buf, "%s%s%d", request_type, vpath, &contentLength);
    if(strcmp(request_type, "READ") == 0) {
        ss_read(client_sockfd, vpath);
    }
    else if(strcmp(request_type, "WRITE") == 0) {
        ss_write(client_sockfd, false, vpath, contentLength);
    }
    else if(strcmp(request_type, "WRITESYNC") == 0) {
        ss_write(client_sockfd, true, vpath, contentLength);
    }
    else if(strcmp(request_type, "CREATE") == 0) {
        ss_create(client_sockfd, vpath);
    }
    else if(strcmp(request_type, "DELETE") == 0) {
        ss_delete(client_sockfd, vpath);
    }
    else if(strcmp(request_type, "STREAM") == 0) {
        ss_stream(client_sockfd, vpath);
    }
    else {
        send(client_sockfd, "tf u sending brother??????????\n", strlen("tf u sending brother??????????\n"), 0);
    }
    close(client_sockfd);
    return NULL;
}

void ss_read(int fd, char* vpath) {
    struct file* f = get_file(vpath);
    if(!f) {
        send(fd, "File not found\n", strlen("File not found\n"), 0);
        return;
    }
    FILE* F = fopen(f->rpath, "r");
    if(!F) {
        send(fd, "File not found\n", strlen("File not found\n"), 0);
        return;
    }
    char buf[8193]; int n = 0;
    sem_wait(&f->lock);
    f->readers++;
    if(f->readers == 1) sem_wait(&f->writelock);
    sem_post(&f->lock);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(fd, buf, n, 0);
    }
    sem_wait(&f->lock);
    f->readers--;
    if(f->readers == 0) sem_post(&f->writelock);
    sem_post(&f->lock);
}

void ss_write(int fd, bool sync, char* vpath, int contentLength) {
    struct file* f = get_file(vpath);
    if(!f) {
        send(fd, "File not found\n", strlen("File not found\n"), 0);
        return;
    }
    FILE* F = fopen(f->rpath, "w");
    char buf[8193]; int n = 0;
    sem_wait(&f->writelock);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(fd, buf, n, 0);
    }
    sem_post(&f->writelock);
}

void* ss_write_helper(void* arg) {
    
}

void ss_create(int fd, char* vpath) {
    char buf[100];
}

void ss_delete(int fd, char* vpath) {
    struct file* f = get_file(vpath);
    if(!f) send(fd, "File not found\n", strlen("File not found\n"), 0);
    else if(remove(f->rpath)) {
        send(fd, "Error deleting file\n", strlen("Error deleting file\n"), 0);
    }
    else {
        send(fd, "File deleted\n", strlen("File deleted\n"), 0);
    }
}

void ss_stream(int fd, char* vpath) {
    struct file* f = get_file(vpath);
    if(!f) {
        send(fd, "File not found\n", strlen("File not found\n"), 0);
        return;
    }
    FILE* F = fopen(f->rpath, "rb");
    if(!F) {
        send(fd, "File not found\n", strlen("File not found\n"), 0);
        return;
    }
    char buf[8193]; int n = 0;
    sem_wait(&f->lock);
    f->readers++;
    if(f->readers == 1) sem_wait(&f->writelock);
    sem_post(&f->lock);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(fd, buf, n, 0);
    }
    sem_wait(&f->lock);
    f->readers--;
    if(f->readers == 0) sem_post(&f->writelock);
    sem_post(&f->lock);
}