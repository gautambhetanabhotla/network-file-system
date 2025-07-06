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
    recv(client_sockfd, &req, sizeof(req), MSG_WAITALL);
    request_to_string(&req);
    switch(req.type) {
        case READ:
            ss_read(&req, client_sockfd);
            break;
        case WRITE:
            ss_write(&req, client_sockfd);
            break;
        case CREATE:
            ss_create(&req, client_sockfd);
            break;
        case DELETE:
            ss_delete(&req, client_sockfd);
            break;
        case STREAM:
            ss_read(&req, client_sockfd);
            break;
        case COPY:
            ss_copy(&req, client_sockfd);
            break;
        case INFO:
            ss_info(&req, client_sockfd);
            break;
        default:
            respond(nm_sockfd, client_sockfd, E_WRONG_SS, req.id, 0, NULL, 0);
            break;
    }
    close(client_sockfd);
    pthread_exit(NULL);
    return NULL;
}

void ss_read(request_header* req, int client_sockfd) {
    struct file* f = get_file(req->paths[0]);
    if(!f) {
        respond(nm_sockfd, client_sockfd, E_WRONG_SS, req->id, 0, NULL, 0);
        return;
    }
    FILE* F = fopen(f->rpath, "r");
    if(!F) {
        respond(nm_sockfd, client_sockfd, E_FAULTY_SS, req->id, 0, NULL, 0);
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
    respond(-1, client_sockfd, ACK, req->id, file_size, NULL, 0);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(client_sockfd, buf, n, 0);
    }
    sem_wait(&f->lock);
    f->readers--;
    if(f->readers == 0) sem_post(&f->writelock);
    sem_post(&f->lock);
}

// struct arg1 {
//     int port;
//     char* ip;
//     char* vpath;
//     struct file* f;
// };

void ss_write(request_header* req, int client_sockfd) {

    struct file* f = get_file(req->paths[0]);
    if(!f) {
        respond(nm_sockfd, client_sockfd, E_WRONG_SS, req->id, 0, NULL, 0);
        return;
    }
    
    // Open and write to the file
    FILE* F = fopen(f->rpath, "w");
    if(!F) {
        respond(nm_sockfd, client_sockfd, E_FAULTY_SS, req->id, 0, NULL, 0);
        return;
    }
    char buf[8193]; int bytes_written = 0;
    sem_wait(&f->serviceQueue);
    sem_wait(&f->writelock);
    sem_post(&f->serviceQueue);
    respond(-1, client_sockfd, ACK, req->id, 0, NULL, 0);
    while(bytes_written < req->contentLength) {
        int k = recv(client_sockfd, buf, 8192, 0);
        if(k == 0) break;
        fwrite(buf, 1, k, F);
        bytes_written += k;
    }
    fclose(F);
    sem_post(&f->writelock);
    if(bytes_written < req->contentLength) respond(nm_sockfd, client_sockfd, E_INCOMPLETE_WRITE, req->id, 0, NULL, 0);
    else respond(nm_sockfd, client_sockfd, SUCCESS, req->id, 0, NULL, 0);

    // TODO: Copy the file to other storage servers
}

struct file* ss_create(request_header* req, int client_sockfd) {
    char* vpath = req->paths[0];
    struct file* g = NULL;
    if((g = get_file(vpath))) {
        respond(nm_sockfd, -1, E_FILE_ALREADY_EXISTS, req->id, 0, NULL, 0);
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
    char mtime[20];
    time_t now = time(NULL);
    struct tm* local_time = localtime(&now);
    strftime(mtime, sizeof(mtime), "%Y-%m-%dT%H:%M:%S", local_time);
    struct file* f = add_file_entry(vpath, rpath, mtime, true);
    if(f == NULL) respond(nm_sockfd, client_sockfd, E_FAULTY_SS, req->id, 0, NULL, 0);
    else respond(nm_sockfd, client_sockfd, SUCCESS, req->id, 0, NULL, 0);
    return f;
}

void ss_delete(request_header* req, int client_sockfd) {
    struct file* f = get_file(req->paths[0]);
    if(!f) respond(nm_sockfd, client_sockfd, E_FILE_DOESNT_EXIST, req->id, 0, NULL, 0);
    else if(remove(f->rpath)) {
        respond(nm_sockfd, client_sockfd, E_FAULTY_SS, req->id, 0, NULL, 0);
    }
    else {
        remove_file_entry(req->paths[0]);
        respond(nm_sockfd, client_sockfd, SUCCESS, req->id, 0, NULL, 0);
    }
}

void ss_copy(request_header* req, int client_sockfd) {
    // struct file* sourcefile = get_file(req->paths[0]);
    // char* dstpath = req->paths[1];

    // if(!sourcefile) {
    //     respond(nm_sockfd, client_sockfd, E_FILE_DOESNT_EXIST, req->id, 0, NULL, 0);
    //     return;
    // }
    // FILE* F = fopen(sourcefile->rpath, "r");
    // fseek(F, 0, SEEK_END);
    // long long file_size = ftell(F);
    // rewind(F);
    // char* paths[] = {dstpath, NULL};
    // request(-1, destfd, WRITE, file_size, paths, NULL, NULL);
    // char buf[8193]; int n = 0;
    // send(destfd, dstpath, strlen(dstpath), 0);
    // while(!feof(F)) {
    //     n = fread(buf, 1, 8192, F);
    //     if(n > 0) send(destfd, buf, n, 0);
    // }
    // fclose(F);
    // respond(nm_sockfd, fd, SUCCESS, requestID, 0, NULL, 0);
}

void ss_info(request_header* req, int client_sockfd) {
    struct file* f = get_file(req->paths[0]);
    if(f == NULL) {
        respond(nm_sockfd, client_sockfd, E_WRONG_SS, req->id, 0, NULL, 0);
        return;
    }
    FILE* F = fopen(f->rpath, "r");
    if(F == NULL) {
        respond(nm_sockfd, client_sockfd, E_FAULTY_SS, req->id, 0, NULL, 0);
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
    respond(nm_sockfd, client_sockfd, SUCCESS, req->id, (long)strlen(buffer), NULL, 0);
    send(client_sockfd, buffer, strlen(buffer), 0);

    fclose(F);
}
