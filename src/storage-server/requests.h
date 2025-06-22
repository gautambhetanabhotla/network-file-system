#include <stdbool.h>

#ifndef REQUESTS_H
#define REQUESTS_H

extern int nm_sockfd;

#define BACKLOG 100
#define MAXPATHLENGTH 4096
#define MAXFILENAMELENGTH 256

/**
 * @enum request_type
 * @brief Represents the types of requests supported by the storage server.
 */
enum request_type {
    READ = 1, // Request to read data from the server.
    WRITE,    // Request to write data to the server.
    STREAM,   // Request to stream data from/to the server.
    INFO,     // Request to retrieve information about a file or resource.
    LIST,     // Request to list files or directories.
    CREATE,   // Request to create a new file or directory.
    COPY,     // Request to copy a file or directory.
    DELETE,   // Request to delete a file or directory.
    SYNC,     // Request to synchronize data with the server.
    HELLO,    // Request to initiate a handshake or greeting with the server.
    SYNC_BACK // Request to synchronize data back from the server.
};

/**
 * @enum ExitStatus
 * @brief Represents the possible exit statuses for requests.
 */
enum exit_status {
    SUCCESS,                // Request completed successfully.
    ACK,                    // Request acknowledged. The server may or may not work on it immediately.
    E_FILE_DOESNT_EXIST,    // File doesn't exist.
    E_INCOMPLETE_WRITE,     // Incomplete write operation.
    E_FILE_ALREADY_EXISTS,  // File already exists.
    E_WRONG_SS,             // The requested file path doesn't exist on this storage server.
    E_FAULTY_SS,            // Something went wrong on the storage server's side.
    E_CONN_REFUSED          // Connection refused.
};

int recv_full(int fd, char* buf, int contentLength);

void* handle_client(void* arg);
void ss_read(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ss_write(int fd, char* vpath, int contentLength, int requestID, char* tbf, int rcl);
struct file* ss_create(int fd, char* vpath, char* mtime, int requestID, int contentLength, char* tbf, int rcl);
void ss_delete(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ss_stream(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ss_copy(int fd, char* srcpath, int requestID, char* tbf, int rcl);
void ss_info(int fd, char* vpath, int requestID, char* tbf, int rcl);
void ss_sync(int fd, char* vpath, int requestID, char* tbf, int rcl);

void request(int clfd, int nmfd, enum request_type type, long contentLength);
void respond(int clfd, int nmfd, enum exit_status status, int requestID, long contentLength);

#endif