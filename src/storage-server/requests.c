#include "requests.h"
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

char* requeststrings[] = {"read", "write", "stream", "info", "list", "create", "copy", "delete", "sync", "hello", "created"};
char* exitstatusstrings[] = {"success", "acknowledge", "file doesn't exist", "incomplete write", "file already exists", "nm chose the wrong ss", "an error from SS's side", "connection refused"};

/**
 * Sends a response with the given exit status, request ID, and content length
 * to the specified client and/or naming server file descriptors.
 *
 * @param nmfd File descriptor for the naming server. If -1, no response is sent to the naming server.
 * @param clfd File descriptor for the client. If -1, no response is sent to the client.
 * @param status The exit status code of the response.
 * @param requestID The unique identifier for the request being responded to.
 * @param contentLength The length of the content being responded with.
 */
void respond(int nmfd, int clfd, enum exit_status status, int requestID, long contentLength) {
    char header[11] = {'\0'}; header[0] = '0' + status;
    sprintf(header + 1, "%d", requestID);
    char CL[21]; CL[20] = '\0';
    sprintf(CL, "%ld", contentLength);
    fprintf(stderr, "Responding with %s for request ID %d\n", exitstatusstrings[status], requestID);
    if(clfd != -1){
        send(clfd, header, sizeof(header) - 1, 0);
        send(clfd, CL, sizeof(CL) - 1, 0);
    }
    if(nmfd != -1) {
        send(nmfd, header, sizeof(header) - 1, 0);
        send(nmfd, CL, sizeof(CL) - 1, 0);
    }
}

void request(int nmfd, int clfd, enum request_type type, long contentLength) {
    char header[11] = {'\0'}; header[0] = '0' + type;
    snprintf(header + 1, 9, "%d", 0);
    char CL[21]; CL[20] = '\0';
    sprintf(CL, "%ld", contentLength);
    fprintf(stderr, "Requesting %s\n", requeststrings[type]);
    if(clfd != -1) {
        send(clfd, header, sizeof(header) - 1, 0);
        send(clfd, CL, sizeof(CL) - 1, 0);
    }
    if(nmfd != -1) {
        send(nmfd, header, sizeof(header) - 1, 0);
        send(nmfd, CL, sizeof(CL) - 1, 0);
    }
}

int recv_full(int fd, char* buf, int contentLength) {
    char buf2[4097]; int n = 0;
    int k;
    while(n < contentLength) {
        k = recv(fd, buf2, contentLength - n, 0);
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
    recv_full(client_sockfd, reqdata, sizeof(reqdata) - 1);
    recv_full(client_sockfd, CL, sizeof(CL) - 1);
    int contentLength = atoi(CL);
    int CLD = contentLength < 4097 ? contentLength : 4097;
    if(reqdata[0] - '0' == WRITE) respond(-1, client_sockfd, ACK, atoi(reqdata + 1), 0);
    recv_full(client_sockfd, vpath, CLD);
    char* fp = vpath;
    while(*fp != '\n') fp++;
    *fp = '\0';
    fp++;
    int remainingContentLength = contentLength - (fp - vpath); // COULD CAUSE ERRORS, CHECK
    int requestID = atoi(reqdata + 1);
    fprintf(stderr, "received request: %s %s, ID %d\n", requeststrings[reqdata[0] - '0' - 1], vpath, requestID);
    switch(reqdata[0] - '0') {
        case READ:
            ss_read(client_sockfd, vpath, requestID, fp, remainingContentLength);
            break;
        case WRITE:
            ss_write(client_sockfd, vpath, contentLength, requestID, fp, remainingContentLength);
            break;
        case CREATE:
            ss_create(client_sockfd, vpath, UNIX_START_TIME, requestID, contentLength, fp, remainingContentLength);
            break;
        case DELETE:
            ss_delete(client_sockfd, vpath, requestID, fp, remainingContentLength);
            break;
        case STREAM:
            ss_stream(client_sockfd, vpath, requestID, fp, remainingContentLength);
            break;
        case COPY:
            ss_copy(client_sockfd, vpath, requestID, fp, remainingContentLength);
            break;
        case INFO:
            ss_info(client_sockfd, vpath, requestID, fp, remainingContentLength);
            break;
        case SYNC:
            ss_sync(client_sockfd, vpath, requestID, fp, remainingContentLength);
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
        respond(nm_sockfd, fd, E_WRONG_SS, requestID, 0);
        return;
    }
    FILE* F = fopen(f->rpath, "r");
    if(!F) {
        respond(nm_sockfd, fd, E_FAULTY_SS, requestID, 0);
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
    respond(-1, fd, ACK, requestID, file_size);
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

void* send_file(void* arg) {
    int port = ((struct arg1 *)arg)->port;
    char* ip = ((struct arg1 *)arg)->ip;
    char* vpath = ((struct arg1 *)arg)->vpath;
    struct file* f = ((struct arg1 *)arg)->f;

    sem_wait(&f->serviceQueue);
    sem_wait(&f->lock);
    f->readers++;
    if(f->readers == 1) sem_wait(&f->writelock);
    sem_post(&f->lock);
    sem_post(&f->serviceQueue);

    if(strcmp(ip, "127.0.0.1") != 0 || port != -1) {
        int destfd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip);
        if(connect(destfd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
            perror("Connect");
        }
        char buf[8193]; int n = 0;

        FILE* F = fopen(f->rpath, "r");
        if(!F) {
            perror("file opening");
        }
        long filesize;
        fseek(F, 0, SEEK_END);
        filesize = ftell(F);
        rewind(F);

        // char buf[123];
        sprintf(buf, "%s\n127.0.0.1\n-1\n127.0.0.1\n-1\n", vpath);
        request(-1, destfd, WRITE, filesize + strlen(buf));
        send(destfd, buf, strlen(buf), 0);

        while(!feof(F)) {
            n = fread(buf, 1, 8192, F);
            if(n > 0) send(destfd, buf, n, 0);
        }
        fclose(F);

        close(destfd);
    }

    sem_wait(&f->lock);
    f->readers--;
    if(f->readers == 0) sem_post(&f->writelock);
    sem_post(&f->lock);
}

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
        respond(nm_sockfd, fd, E_WRONG_SS, requestID, 0);
        return;
    }
    if(!f) {
        respond(nm_sockfd, fd, E_FAULTY_SS, requestID, 0);
        return;
    }

    // Open and write to the file
    FILE* F = fopen(f->rpath, "w");
    char buf[8193]; int n = 0;
    sem_wait(&f->serviceQueue);
    sem_wait(&f->writelock);
    sem_post(&f->serviceQueue);
    respond(-1, fd, ACK, requestID, 0);
    n += fwrite(tbf + rcl - contentLength, 1, rcl, F);
    while(n < contentLength) {
        int k = recv(fd, buf, 8192, 0);
        if(k == 0) break;
        fwrite(buf, 1, k, F);
        n += k;
    }
    fclose(F);
    sem_post(&f->writelock);
    if(n < contentLength) respond(nm_sockfd, fd, E_INCOMPLETE_WRITE, requestID, 0);
    else respond(nm_sockfd, fd, SUCCESS, requestID, 0);

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
    pthread_create(&t1, NULL, send_file, arg);
    pthread_create(&t2, NULL, send_file, arg2);
}

struct file* ss_create(int fd, char* vpath, char* mtime, int requestID, int contentLength, char* tbf, int rcl) {
    contentLength -= (strlen(vpath) + 1);
    tbf[contentLength] = '\0';
    mtime = tbf + 3;
    struct file* g = NULL;
    if((g = get_file(vpath))) {
        respond(nm_sockfd, -1, E_FILE_ALREADY_EXISTS, requestID, 0);
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
    if(f == NULL) respond(nm_sockfd, fd, E_FAULTY_SS, requestID, 0);
    else respond(nm_sockfd, fd, SUCCESS, requestID, 0);
    return f;
}

void ss_delete(int fd, char* vpath, int requestID, char* tbf, int rcl) {
    struct file* f = get_file(vpath);
    if(!f) respond(nm_sockfd, fd, E_FILE_DOESNT_EXIST, requestID, 0);
    else if(remove(f->rpath)) {
        respond(nm_sockfd, -1, E_FAULTY_SS, requestID, 0);
    }
    else {
        remove_file_entry(vpath);
        respond(nm_sockfd, -1, SUCCESS, requestID, 0);
    }
}

void ss_stream(int fd, char* vpath, int requestID, char* tbf, int rcl) {
    struct file* f = get_file(vpath);
    if(!f) {
        respond(nm_sockfd, fd, E_WRONG_SS, requestID, 0);
        return;
    }
    FILE* F = fopen(f->rpath, "rb");
    if(!F) {
        respond(nm_sockfd, fd, E_FAULTY_SS, requestID, 0);
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
    respond(-1, fd, file_size, requestID, file_size);
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
        respond(nm_sockfd, fd, E_CONN_REFUSED, requestID, 0);
    }
    free(IP);
    if(!f) {
        respond(nm_sockfd, fd, E_FILE_DOESNT_EXIST, requestID, 0);
        return;
    }
    FILE* F = fopen(f->rpath, "r");
    fseek(F, 0, SEEK_END);
    long file_size = ftell(F);
    rewind(F);
    request(-1, destfd, WRITE, file_size);
    char buf[8193]; int n = 0;
    send(destfd, dstpath, strlen(dstpath), 0);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(destfd, buf, n, 0);
    }
    fclose(F);
    respond(nm_sockfd, fd, SUCCESS, requestID, 0);
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

    struct stat statbuf;
    if(lstat(f->rpath, &statbuf) == -1) {
        perror("lstat");
    }
    char permsbuf[11];
    sprintf(permsbuf, "%c%c%c%c%c%c%c%c%c%c",
            (S_ISDIR(statbuf.st_mode)) ? 'd' : '.',
            (statbuf.st_mode & S_IRUSR) ? 'r' : '-',
            (statbuf.st_mode & S_IWUSR) ? 'w' : '-',
            (statbuf.st_mode & S_IXUSR) ? 'x' : '-',
            (statbuf.st_mode & S_IRGRP) ? 'r' : '-',
            (statbuf.st_mode & S_IWGRP) ? 'w' : '-',
            (statbuf.st_mode & S_IXGRP) ? 'x' : '-',
            (statbuf.st_mode & S_IROTH) ? 'r' : '-',
            (statbuf.st_mode & S_IWOTH) ? 'w' : '-',
            (statbuf.st_mode & S_IXOTH) ? 'x' : '-');
    permsbuf[10] = '\0';

    // Send the file info to the client
    char buffer[1024];
    sprintf(buffer, "File size: %ld bytes\nNumber of lines: %d\nLast modified: %s\nPermissions: %s\n", file_size, line_count, f->mtime, permsbuf);
    respond(nm_sockfd, fd, SUCCESS, requestID, (long)strlen(buffer));
    send(fd, buffer, strlen(buffer), 0);

    fclose(F);
}

void ss_sync(int fd, char* vpath, int requestID, char* tbf, int rcl) {
    struct file* f = get_file(vpath);
    if(!f) {
        respond(nm_sockfd, fd, E_FILE_DOESNT_EXIST, requestID, 0);
        return;
    }
    FILE* F = fopen(f->rpath, "r");
    if(!F) {
        respond(nm_sockfd, fd, E_FAULTY_SS, requestID, 0);
        return;
    }

    long filesize;
    fseek(F, 0, SEEK_END);
    filesize = ftell(F);
    rewind(F);

    char* portptr;
    char* ip = __strtok_r(tbf, "\n", &portptr);
    __strtok_r(NULL, "\n", &tbf);
    int port = atoi(portptr);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    int destfd = socket(AF_INET, SOCK_STREAM, 0);
    if(connect(destfd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        respond(nm_sockfd, fd, E_CONN_REFUSED, requestID, 0);
    }
    sem_wait(&f->serviceQueue);
    sem_wait(&f->lock);
    f->readers++;
    if(f->readers == 1) sem_wait(&f->writelock);
    sem_post(&f->lock);
    sem_post(&f->serviceQueue);
    request(-1, destfd, WRITE, filesize + strlen(vpath) + 1);
    char buf[8193]; int n = 0;
    char pathbuf[MAXPATHLENGTH];
    sprintf(pathbuf, "%s\n", vpath);
    send(destfd, pathbuf, strlen(pathbuf), 0);
    while(!feof(F)) {
        n = fread(buf, 1, 8192, F);
        if(n > 0) send(destfd, buf, n, 0);
    }
    sem_wait(&f->lock);
    f->readers--;
    if(f->readers == 0) sem_post(&f->writelock);
    sem_post(&f->lock);
    fclose(F);
    respond(nm_sockfd, fd, SUCCESS, requestID, 0);
}
