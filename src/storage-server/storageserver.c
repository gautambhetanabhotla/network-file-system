#include "storageserver.h"
#include "../lib/request.h"
#include "files.h"

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/stat.h>

// Format: YYYY-MM-DDTHH:MM:SS
#define UNIX_START_TIME "1970-01-01T00:00:00"

extern struct file** file_entries;
extern unsigned long long int n_file_entries;

extern int nm_sockfd;

// int recv_full(int fd, char* buf, int contentLength) {
//     char buf2[4097]; int n = 0;
//     int k;
//     while(n < contentLength) {
//         k = recv(fd, buf2, contentLength - n, 0);
//         if(k <= 0) break;
//         memcpy(buf + n, buf2, k);
//         n += k;
//     }
//     if(k < 0) return 2; // For other error
//     if(n < contentLength) return 1; // For premature termination
//     else return 0; // Success
// }

/**
 * Handles a client connection by processing the request and responding accordingly.
 *
 * @param arg Pointer to the client socket file descriptor.
 * @return NULL
 */
void* handle_client(void* arg) {
    fprintf(stderr, "Client arrived!\n");
    int client_sockfd = *(int*)arg;
    request_header req;
    recv(client_sockfd, &req, sizeof(req), 0);
    request_to_string(&req);
    switch(req.type) {
        case READ:
            ss_read(client_sockfd, req.paths[0], req.id, req.paths[1], req.contentLength);
            break;
        case WRITE:
            ss_write(client_sockfd, req.paths[0], req.contentLength, req.id, req.paths[1], req.contentLength);
            break;
        case CREATE:
            ss_create(client_sockfd, req.paths[0], UNIX_START_TIME, req.id, req.contentLength, req.paths[1], req.contentLength);
            break;
        case DELETE:
            ss_delete(client_sockfd, req.paths[0], req.id, req.paths[1], req.contentLength);
            break;
        case STREAM:
            ss_stream(client_sockfd, req.paths[0], req.id, req.paths[1], req.contentLength);
            break;
        case COPY:
            ss_copy(client_sockfd, req.paths[0], req.id, req.paths[1], req.contentLength);
            break;
        case INFO:
            ss_info(client_sockfd, req.paths[0], req.id, req.paths[1], req.contentLength);
            break;
        default:
            send(client_sockfd, "tf u sending brother??????????\n", strlen("tf u sending brother??????????\n"), 0);
            break;
    }
    close(client_sockfd);
    pthread_exit(NULL);
    return NULL;
}

void ss_read(int fd, char* vpath, int requestID, char* tbf, int rcl) {
    struct file* f = get_file(vpath);
    if(!f) {
        respond(nm_sockfd, fd, E_WRONG_SS, requestID, 0, NULL, 0);
        return;
    }
    FILE* F = fopen(f->rpath, "r");
    if(!F) {
        respond(nm_sockfd, fd, E_FAULTY_SS, requestID, 0, NULL, 0);
        return;
    }
    fseek(F, 0, SEEK_END);
    long file_size = ftell(F);
    rewind(F);
    char buf[8193]; int n = 0;
    sem_wait(&f->serviceQueue);
    sem_wait(&f->lock);
    f->readers++;
    if(f->readers == 1) sem_wait(&f->writelock);
    sem_post(&f->lock);
    sem_post(&f->serviceQueue);
    respond(-1, fd, ACK, requestID, file_size, NULL, 0);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(fd, buf, n, 0);
    }
    sem_wait(&f->lock);
    f->readers--;
    if(f->readers == 0) sem_post(&f->writelock);
    sem_post(&f->lock);
}

struct arg1 {
    int port;
    char* ip;
    char* vpath;
    struct file* f;
};

void ss_write(int fd, char* vpath, int contentLength, int requestID, char* tbf, int rcl) {
    char* tbf_old = tbf;
    contentLength = rcl;
    // contentLength -= (strlen(vpath) + 1);
    
    // Obtain IPs and ports of other Storage servers
    char *ip1, *ip2, *port1, *port2, *saveptr;
    ip1 = __strtok_r(tbf, "\n", &saveptr);
    port1 = __strtok_r(NULL, "\n", &saveptr);
    ip2 = __strtok_r(NULL, "\n", &saveptr);
    port2 = __strtok_r(NULL, "\n", &saveptr);
    contentLength -= (saveptr - tbf_old);
    int p1 = atoi(port1), p2 = atoi(port2);

    struct file* f = get_file(vpath);
    if(!f) {
        respond(nm_sockfd, fd, E_WRONG_SS, requestID, 0, NULL, 0);
        return;
    }
    if(!f) {
        respond(nm_sockfd, fd, E_FAULTY_SS, requestID, 0, NULL, 0);
        return;
    }

    // Open and write to the file
    FILE* F = fopen(f->rpath, "w");
    char buf[8193]; int n = 0;
    sem_wait(&f->serviceQueue);
    sem_wait(&f->writelock);
    sem_post(&f->serviceQueue);
    respond(-1, fd, ACK, requestID, 0, NULL, 0);
    n += fwrite(tbf + rcl - contentLength, 1, rcl, F);
    while(n < contentLength) {
        int k = recv(fd, buf, 8192, 0);
        if(k == 0) break;
        fwrite(buf, 1, k, F);
        n += k;
    }
    fclose(F);
    sem_post(&f->writelock);
    if(n < contentLength) respond(nm_sockfd, fd, E_INCOMPLETE_WRITE, requestID, 0, NULL, 0);
    else respond(nm_sockfd, fd, SUCCESS, requestID, 0, NULL, 0);

    // Copy the file to other Storage servers
    pthread_t t1, t2;
    struct arg1* arg = (struct arg1*)malloc(sizeof(struct arg1));
    arg->port = p1;
    arg->ip = strdup(ip1);
    arg->vpath = strdup(vpath);
    arg->f = f;
    struct arg1* arg2 = (struct arg1*)malloc(sizeof(struct arg1));
    arg2->port = p2;
    arg2->ip = strdup(ip2);
    arg2->vpath = strdup(vpath);
    arg2->f = f;
    // pthread_create(&t1, NULL, send_file, arg);
    // pthread_create(&t2, NULL, send_file, arg2);
}

struct file* ss_create(int fd, char* vpath, char* mtime, int requestID, int contentLength, char* tbf, int rcl) {
    contentLength -= (strlen(vpath) + 1);
    tbf[contentLength] = '\0';
    mtime = tbf + 3;
    struct file* g = NULL;
    if((g = get_file(vpath))) {
        respond(nm_sockfd, -1, E_FILE_ALREADY_EXISTS, requestID, 0, NULL, 0);
        return g;
    }
    char rpath[MAXPATHLENGTH + 1];
    int pp = 1;
    sprintf(rpath, "%s%llu", "storage/", n_file_entries + pp);
    FILE* p = fopen(rpath, "r");
    while(p) {
        fclose(p);
        pp++;
        sprintf(rpath, "%s%llu", "storage/", n_file_entries + pp);
        p = fopen(rpath, "r");
    }
    // fclose(p);
    FILE* ff = fopen(rpath, "w");
    fclose(ff);
    struct file* f = add_file_entry(vpath, rpath, mtime, true);
    if(f == NULL) respond(nm_sockfd, fd, E_FAULTY_SS, requestID, 0, NULL, 0);
    else respond(nm_sockfd, fd, SUCCESS, requestID, 0, NULL, 0);
    return f;
}

void ss_delete(int fd, char* vpath, int requestID, char* tbf, int rcl) {
    struct file* f = get_file(vpath);
    if(!f) respond(nm_sockfd, fd, E_FILE_DOESNT_EXIST, requestID, 0, NULL, 0);
    else if(remove(f->rpath)) {
        respond(nm_sockfd, -1, E_FAULTY_SS, requestID, 0, NULL, 0);
    }
    else {
        remove_file_entry(vpath);
        respond(nm_sockfd, -1, SUCCESS, requestID, 0, NULL, 0);
    }
}

void ss_stream(int fd, char* vpath, int requestID, char* tbf, int rcl) {
    struct file* f = get_file(vpath);
    if(!f) {
        respond(nm_sockfd, fd, E_WRONG_SS, requestID, 0, NULL, 0);
        return;
    }
    FILE* F = fopen(f->rpath, "rb");
    if(!F) {
        respond(nm_sockfd, fd, E_FAULTY_SS, requestID, 0, NULL, 0);
        return;
    }
    fseek(F, 0, SEEK_END);
    long file_size = ftell(F);
    rewind(F);
    char buf[8193]; int n = 0;
    sem_wait(&f->serviceQueue);
    sem_wait(&f->lock);
    f->readers++;
    if(f->readers == 1) sem_wait(&f->writelock);
    sem_post(&f->lock);
    sem_post(&f->serviceQueue);
    respond(-1, fd, file_size, requestID, file_size, NULL, 0);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(fd, buf, n, 0);
    }
    sem_wait(&f->lock);
    f->readers--;
    if(f->readers == 0) sem_post(&f->writelock);
    sem_post(&f->lock);
}

void ss_copy(int fd, char* srcpath, int requestID, char* tbf, int rcl) {
    struct file* f = get_file(srcpath);
    char *fp = tbf, *fpp = fp;
    while(*fp != '\n') fp++;
    *fp = '\0';
    fp++;
    char* dstpath = (char*) malloc(4097 * sizeof(char));
    strcpy(dstpath, fpp);
    dstpath[strlen(dstpath)] = '\n';
    fpp = fp;
    while(*fp != '\n') fp++;
    *fp = '\0';
    fp++;
    char* IP = (char*) malloc(100 * sizeof(char));
    strcpy(IP, fpp);
    fpp = fp;
    while(*fp != '\n') fp++;
    *fp = '\0';
    fp++;
    int port = atoi(fpp);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(IP);
    int destfd = socket(AF_INET, SOCK_STREAM, 0);
    if(connect(destfd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        respond(nm_sockfd, fd, E_CONN_REFUSED, requestID, 0, NULL, 0);
    }
    free(IP);
    if(!f) {
        respond(nm_sockfd, fd, E_FILE_DOESNT_EXIST, requestID, 0, NULL, 0);
        return;
    }
    FILE* F = fopen(f->rpath, "r");
    fseek(F, 0, SEEK_END);
    long long file_size = ftell(F);
    rewind(F);
    char* paths[] = {dstpath, NULL};
    request(-1, destfd, WRITE, file_size, paths, NULL, NULL);
    char buf[8193]; int n = 0;
    send(destfd, dstpath, strlen(dstpath), 0);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(destfd, buf, n, 0);
    }
    fclose(F);
    respond(nm_sockfd, fd, SUCCESS, requestID, 0, NULL, 0);
}

void ss_info(int fd, char* vpath, int requestID, char* tbf, int rcl) {
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
    respond(nm_sockfd, fd, SUCCESS, requestID, (long)strlen(buffer), NULL, 0);
    send(fd, buffer, strlen(buffer), 0);

    fclose(F);
}
