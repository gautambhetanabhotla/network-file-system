#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <semaphore.h>
#include <netinet/in.h>
#include <pthread.h>

// Maximum sizes
#define MAX_STORAGE_SERVERS 10
#define MAX_CLIENTS 100
#define MAX_PATH_LENGTH 1024
#define MAX_PATHS_PER_SS 1000
#define MAX_MESSAGE_SIZE 1024
#define CACHE_SIZE 100 
#define MAX_COMMAND_SIZE 2048
#define MAX_CLIENTS 100
#define MAX_FILENAME_LENGTH 1024
#define MAX_IP_LENGTH 16
#define PORT_LEN 6

// Error Codes
typedef enum {
    ERR_NONE,
    ERR_FILE_NOT_FOUND,
    ERR_FILE_LOCKED,
    ERR_PERMISSION_DENIED,
    ERR_INVALID_REQUEST,
    ERR_SERVER_UNAVAILABLE
} ErrorCode;




#endif