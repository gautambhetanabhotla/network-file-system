#include "requests.h"
#include "files.h"

#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

extern struct file** file_entries;
extern unsigned long long int n_file_entries;

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
<mtime>
<content length>
<content>

COPY
<source vpath>
<dest vpath>

STREAM
<virtual path>

CREATE
<virtual path>
<mtime>

*/

void* handle_client(void* arg) {
    fprintf(stderr, "Client arrived!\n");
    int client_sockfd = *(int*)arg;
    char request_type[11], vpath[MAXPATHLENGTH + 1];
    recv(client_sockfd, request_type, sizeof(request_type) - 1, 0);
    recv(client_sockfd, vpath, sizeof(vpath) - 1, 0);
    if(strcmp(request_type, "READ") == 0) {
        ss_read(client_sockfd, vpath);
    }
    else if(strcmp(request_type, "WRITE") == 0) {
        char mtime[21], CL[21];
        recv(client_sockfd, mtime, sizeof(mtime) - 1, 0);
        recv(client_sockfd, CL, sizeof(CL) - 1, 0);
        ss_write(client_sockfd, vpath, mtime, atoi(CL));
    }
    else if(strcmp(request_type, "CREATE") == 0) {
        char mtime[21];
        recv(client_sockfd, mtime, sizeof(mtime) - 1, 0);
        ss_create(client_sockfd, vpath, mtime);
    }
    else if(strcmp(request_type, "DELETE") == 0) {
        ss_delete(client_sockfd, vpath);
    }
    else if(strcmp(request_type, "STREAM") == 0) {
        ss_stream(client_sockfd, vpath);
    }
    else if(strcmp(request_type, "COPY") == 0) {
        ss_copy(client_sockfd, vpath);
    }
    else {
        send(client_sockfd, "tf u sending brother??????????\n", strlen("tf u sending brother??????????\n"), 0);
    }
    close(client_sockfd);
    pthread_exit(NULL);
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
    sem_wait(&f->serviceQueue);
    sem_wait(&f->lock);
    f->readers++;
    if(f->readers == 1) sem_wait(&f->writelock);
    sem_post(&f->lock);
    sem_post(&f->serviceQueue);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(fd, buf, n, 0);
    }
    sem_wait(&f->lock);
    f->readers--;
    if(f->readers == 0) sem_post(&f->writelock);
    sem_post(&f->lock);
}

void ss_write(int fd, char* vpath, char* mtime, int contentLength) {
    struct file* f = get_file(vpath);
    if(!f) {
        send(fd, "File not found\n", strlen("File not found\n"), 0);
        return;
    }
    FILE* F = fopen(f->rpath, "w");
    char buf[8193]; int n = 0;
    sem_wait(&f->serviceQueue);
    sem_wait(&f->writelock);
    sem_post(&f->serviceQueue);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(fd, buf, n, 0);
    }
    sem_post(&f->writelock);
}

void ss_create(int fd, char* vpath, char* mtime) {
    char rpath[MAXPATHLENGTH + 1];
    sprintf(rpath, "%s%llu", "storage/", n_file_entries + 1);
    if(add_file_entry(vpath, rpath, mtime, true)) {
        send(fd, "Error creating file\n", strlen("Error creating file\n"), 0);
    }
    else {
        send(fd, "File created\n", strlen("File created\n"), 0);
        FILE* F = fopen(rpath, "w");
        fclose(F);
    }
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
    sem_wait(&f->serviceQueue);
    sem_wait(&f->lock);
    f->readers++;
    if(f->readers == 1) sem_wait(&f->writelock);
    sem_post(&f->lock);
    sem_post(&f->serviceQueue);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(fd, buf, n, 0);
    }
    sem_wait(&f->lock);
    f->readers--;
    if(f->readers == 0) sem_post(&f->writelock);
    sem_post(&f->lock);
}

void ss_copy(int fd, char* vpath) {

}

void ss_info(int fd, char* vpath) {
    
}