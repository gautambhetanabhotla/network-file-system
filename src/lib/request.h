#include <stdint.h>
#include <linux/limits.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#ifndef REQUEST_H
#define REQUEST_H

#define BACKLOG 100
#define MAXPATHLENGTH 4096
#define MAXFILENAMELENGTH 256

int connect_with_ip_port(const char* ip, uint16_t port);

/**
 * @enum request_type
 * @brief Represents the types of requests supported by the storage server.
 */
enum request_type {
    READ = 1,             // Request to read data from the server.
    WRITE,                // Request to write data to the server.
    STREAM,               // Request to stream data from/to the server.
    INFO,                 // Request to retrieve information about a file or resource.
    LIST,                 // Request to list files or directories.
    CREATE,               // Request to create a new file or directory.
    COPY,                 // Request to copy a file or directory.
    DELETE,               // Request to delete a file or directory.
    HELLO_FROM_CLIENT,    // Request to initiate a handshake or greeting with the server.
    HELLO_FROM_SS,        // Hello from a storage server. The request body contains the port number and the list of stored paths.
    SYNC_BACK             // Request to synchronize data back from the server.
};

/**
 * @struct request_header
 * @brief Represents the header of a request sent between any two hosts.
 * @typedef request_header
 * @param id Unique identifier for the request.
 * @param contentLength Length of the content in the request body.
 * @param type Type of the request, represented by the `request_type` enum.
 * @param paths Array of two strings representing the paths involved in the request.
 * @brief The header of a request sent between any 2 hosts.
 */
typedef struct request_header {
    uint64_t id;
    uint64_t contentLength;
    enum request_type type;
    char paths[2][PATH_MAX];
    char ip[2][INET_ADDRSTRLEN];
    uint16_t port[2];
} request_header;

void request_to_string(request_header* req);
void request(int fd1, int fd2, enum request_type type, long contentLength, char** paths, char** ips, uint16_t* ports);

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

typedef struct response_header {
    uint64_t requestID;
    uint64_t contentLength;
    enum exit_status status;
    char ip[INET_ADDRSTRLEN];
    uint16_t port;
} response_header;

void respond(int fd1, int fd2, enum exit_status status, int requestID, long contentLength, char* ip, uint16_t port);

#endif
