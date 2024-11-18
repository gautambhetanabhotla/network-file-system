#include "requests.h"
#include "files.h"

#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

// FORMAT: YYYY-MM-DDTHH:MM:SS
#define UNIX_START_TIME "1970-01-01T00:00:00"

extern struct file** file_entries;
extern unsigned long long int n_file_entries;

enum request_type {
    READ = 1, WRITE, STREAM, INFO, LIST, CREATE, COPY, DELETE
};

enum exit_status {
    E_SUCCESS, E_FILE_DOESNT_EXIST, E_INCOMPLETE_WRITE, E_FILE_ALREADY_EXISTS, E_WRONG_SS, E_FAULTY_SS
};

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

HELLO

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

CREATE
<virtual path>

Normalised:
10 - Request type
4096 - Virtual path
20 - content length
Header size = 10 + 4096 + 20 = 4126

*/

extern int nm_sockfd;

void ns_synchronize(int fd, char* vpath, int requestID) {
    
}

int recv_full(int fd, char* buf, int contentLength) {
    char buf2[4097]; int n = 0;
    int k;
    while(n < contentLength) {
        k = recv(fd, buf2, 4096, 0);
        if(k <= 0) break;
        memcpy(buf + n, buf2, k);
        n += k;
    }
    if(k < 0) return 2; // For other error
    if(n < contentLength) return 1; // For premature termination
    else return 0; // Success
}

void* handle_client(void* arg) {
    fprintf(stderr, "Client arrived!\n");
    int client_sockfd = *(int*)arg;
    char reqdata[11], vpath[MAXPATHLENGTH + 2], CL[21];
    vpath[4097] = '\0';
    // recv(client_sockfd, reqdata, sizeof(reqdata) - 1, 0);
    recv_full(client_sockfd, reqdata, sizeof(reqdata) - 1);
    // recv(client_sockfd, CL, sizeof(CL) - 1, 0);
    recv_full(client_sockfd, CL, sizeof(CL) - 1);
    // recv(client_sockfd, vpath, sizeof(vpath), 0);
    int contentLength = atoi(CL);
    int CLD = contentLength < 4097 ? contentLength : 4097;
    recv_full(client_sockfd, vpath, CLD);
    char* fp = vpath;
    while(*fp != '\n') fp++;
    *fp = '\0';
    fp++;
    int remainingContentLength = fp - vpath + CLD; // COULD CAUSE ERRORS, CHECK
    int requestID = atoi(reqdata + 1);
    switch(reqdata[0] - '0') {
        case READ:
            ss_read(client_sockfd, vpath, requestID);
            break;
        case WRITE:
            ss_write(client_sockfd, vpath, contentLength, requestID);
            break;
        case CREATE:
            ss_create(client_sockfd, vpath, UNIX_START_TIME, requestID);
            break;
        case DELETE:
            ss_delete(client_sockfd, vpath, requestID);
            break;
        case STREAM:
            ss_stream(client_sockfd, vpath, requestID);
            break;
        case COPY:
            char vpath2[MAXPATHLENGTH + 1];
            recv(client_sockfd, vpath2, sizeof(vpath2) - 1, 0);
            ss_copy(client_sockfd, vpath, vpath2, requestID);
            break;
        case INFO:
            ss_info(client_sockfd, vpath, requestID);
            break;
        default:
            send(client_sockfd, "tf u sending brother??????????\n", strlen("tf u sending brother??????????\n"), 0);
            break;
    }
    close(client_sockfd);
    pthread_exit(NULL);
    return NULL;
}

void ss_read(int fd, char* vpath, int requestID) {
    struct file* f = get_file(vpath);
    if(!f) {
        long file_size = 0;
        char header[20] = {'\0'}; header[0] = '0' + E_WRONG_SS;
        sprintf(header + 10, "%ld", file_size);
        send(fd, header, 20, 0);
        return;
    }
    FILE* F = fopen(f->rpath, "r");
    if(!F) {
        long file_size = 0;
        char header[20] = {'\0'}; header[0] = '0' + E_FAULTY_SS;
        sprintf(header + 10, "%ld", file_size);
        send(fd, header, 20, 0);
        return;
    }
    fseek(F, 0, SEEK_END);
    long file_size = ftell(F);
    rewind(F);
    char header[20] = {'\0'}; header[0] = '0';
    sprintf(header + 10, "%ld", file_size);
    send(fd, header, 20, 0);
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

void ss_write(int fd, char* vpath, int contentLength, int requestID) {
    struct file* f = get_file(vpath);
    if(!f) f = ss_create(fd, vpath, UNIX_START_TIME, requestID);
    if(!f) {
        send(fd, "File not found\n", strlen("File not found\n"), 0);
        return;
    }
    FILE* F = fopen(f->rpath, "w");
    char buf[8193]; int n = 0;
    sem_wait(&f->serviceQueue);
    sem_wait(&f->writelock);
    sem_post(&f->serviceQueue);
    while(n < contentLength) {
        int k = recv(fd, buf, 8192, 0);
        if(k == 0) break;
        fwrite(buf, 1, k, F);
        n += k;
    }
    sem_post(&f->writelock);
    if(n < contentLength) send(fd, "Incomplete write: Client disconnected\n", strlen("Incomplete write: Client disconnected\n"), 0);
}

struct file* ss_create(int fd, char* vpath, char* mtime, int requestID) {
    char rpath[MAXPATHLENGTH + 1];
    int pp = 1;
    sprintf(rpath, "%s%llu", "storage/", n_file_entries + pp);
    FILE* p = fopen(rpath, "r");
    while(p) {
        pp++;
        sprintf(rpath, "%s%llu", "storage/", n_file_entries + pp);
        p = fopen(rpath, "r");
    }
    fclose(p);
    FILE* ff = fopen(rpath, "w");
    fclose(ff);
    struct file* f = add_file_entry(vpath, rpath, mtime, true);
    if(f == NULL) {
        send(fd, "Error creating file\n", strlen("Error creating file\n"), 0);
    }
    else {
        send(fd, "File created\n", strlen("File created\n"), 0);
    }
    char CL[21]; CL[20] = '\0';
    sprintf(CL, "%ld", strlen(vpath));
    send(nm_sockfd, CL, sizeof(CL) - 1, 0);
    send(nm_sockfd, vpath, strlen(vpath), 0);
    return f;
}

void ss_delete(int fd, char* vpath, int requestID) {
    struct file* f = get_file(vpath);
    if(!f) send(fd, "File not found\n", strlen("File not found\n"), 0);
    else if(remove(f->rpath)) {
        send(fd, "Error deleting file\n", strlen("Error deleting file\n"), 0);
    }
    else {
        remove_file_entry(vpath);
        send(fd, "File deleted\n", strlen("File deleted\n"), 0);
    }
}

void ss_stream(int fd, char* vpath, int requestID) {
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

void ss_copy(int fd, char* vpath1, char* vpath2, int requestID) {

}

void ss_info(int fd, char* vpath, int requestID) {
    struct file* f = get_file(vpath);
    if(f == NULL) {
        send(fd, "File not found\n", strlen("File not found\n"), 0);
        return;
    }
    FILE* F = fopen(f->rpath, "r");
    if(F == NULL) {
        send(fd, "File not found\n", strlen("File not found\n"), 0);
        return;
    }

    // Get the file size
    fseek(F, 0, SEEK_END);
    long file_size = ftell(F);
    rewind(F);

    // Count the number of lines
    int line_count = 0;
    char c;
    while ((c = fgetc(F)) != EOF) {
        if (c == '\n') {
            line_count++;
        }
    }
    rewind(F);

    // Send the file info to the client
    char buffer[1024];
    sprintf(buffer, "File size: %ld bytes\nNumber of lines: %d\nLast modified: %s\n", file_size, line_count, f->mtime);
    send(fd, buffer, strlen(buffer), 0);

    fclose(F);
}

void ss_update_mtime(int fd, char* vpath, char* mtime, int requestID) {
    struct file* f = get_file(vpath);
    if(f == NULL) {
        send(fd, "File not found\n", strlen("File not found\n"), 0);
        return;
    }
    strcpy(f->mtime, mtime);
}