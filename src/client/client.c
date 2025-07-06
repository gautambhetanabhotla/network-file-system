#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <ao/ao.h>
#include <pthread.h>
#include <linux/limits.h>

#include "../lib/request.h"

#define TIMEOUT 5 // TIMEOUT in seconds, if ACK is not received in TIMEOUT 
// #define FILEPATH_SIZE 4096
#define BUFFER_SIZE (2 * PATH_MAX + 200)
#define ASYNC_SIZE 2000 // if it >= ASYNC_SIZE, it will be sent asynchronously if there is no --SYNC flag

typedef struct {
    FILE *file;
    int ss_socket;
} ThreadArgs;

int ns_socket; // socket for the naming server

// accepted operations: 
//READ filepath
//WRITE sourcefilepath destfilepath
//CREATE folderpath (name) (name can be file or folder)
//DELETE folderpath (name) (name can be file or folder)
//COPY sourcepath destpath (assumes only for files)
//INFO filepath (filesize, last modified time, whatever)
//STREAM audiofilepath
//LIST folderpath
// flag --SYNC will be considered too, ASSUMPTION: it must be at the end
// ASSUMPTION only write can be asynchronous

// CREATE operation
int create(const char *folderpath, const char *fpath) {
    request(ns_socket, -1, CREATE, 0, (char *[]){(char *)folderpath, (char *)fpath, NULL}, NULL, NULL);
    response_header response;
    ssize_t ns_bytes_received = recv(ns_socket, &response, sizeof(response), MSG_WAITALL);
    if (ns_bytes_received < sizeof(response)) {
        perror("Failed to receive full response from naming server\n");
        return -1;
    }
    switch (response.status) {
        case SUCCESS:
            printf("Successfully created file %s at folder %s\n", fpath, folderpath);
            return 0;
        case E_FILE_ALREADY_EXISTS:
            printf("File or folder already exists at %s/%s\n", folderpath, fpath);
            return -1;
        case E_CONN_REFUSED:
            printf("Connection refused by naming server.\n");
            return -1;
        default:
            printf("An error occurred while creating %s at %s: %d\n", fpath, folderpath, response.status);
            return -1;
    }
}

// DELETE operation
int delete(const char *folderpath, const char *fpath) {
    request(ns_socket, -1, DELETE, 0, (char *[]){(char *)folderpath, (char *)fpath, NULL}, NULL, NULL);
}

// COPY operation
int copy(const char *source_filepath, const char *dest_folderpath) {
    request(ns_socket, -1, COPY, 0, (char *[]){(char *)source_filepath, (char *)dest_folderpath, NULL}, NULL, NULL);
}

int info(const char * filepath) {

    request(ns_socket, -1, INFO, 0, (char *[]){(char *)filepath, NULL}, NULL, NULL);
    response_header response;
    ssize_t ns_bytes_received = recv(ns_socket, &response, sizeof(response), MSG_WAITALL);

    if (ns_bytes_received < sizeof(response)) {
        perror("Failed to receive full response from naming server\n");
        return -1;
    }

    int ss_sockfd = connect_with_ip_port(response.ip, response.port);
    if (ss_sockfd < 0) {
        fprintf(stderr, "Failed to connect to storage server at %s:%d\n", response.ip, response.port);
        return -1;
    }
    fprintf(stderr, "Connected to storage server at %s:%d\n", response.ip, response.port);

    request(ss_sockfd, -1, INFO, 0, (char *[]){(char *)filepath, NULL}, NULL, NULL);
    response_header ss_response;
    ssize_t ss_bytes_received = recv(ss_sockfd, &ss_response, sizeof(ss_response), MSG_WAITALL);
    response_to_string(&ss_response);
    if (ss_bytes_received < sizeof(ss_response)) {
        perror("Failed to receive full response from storage server\n");
        close(ss_sockfd);
        return -1;
    }

    uint64_t data_bytes_received = 0;
    while (data_bytes_received < ss_response.contentLength) {
        char data_buffer[BUFFER_SIZE];
        ssize_t n = recv(ss_sockfd, data_buffer, sizeof(data_buffer), 0);
        if (n < 0) {
            perror("Failed to receive data from storage server\n");
            close(ss_sockfd);
            return -1;
        }
        if (n == 0) break;
        fwrite(data_buffer, 1, n, stdout); // Print the received data
        data_bytes_received += n;
    }

    close(ss_sockfd);
    return 0;
}

// LIST operation
int list(const char *folderpath) {
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "%s\n", folderpath);
    // return ns_request_print(5, request);
}

int read_it(const char * filepath){
    
    request(ns_socket, -1, READ, 0, (char *[]){(char *)filepath, NULL}, NULL, NULL);
    response_header response;
    ssize_t ns_bytes_received = recv(ns_socket, &response, sizeof(response), MSG_WAITALL);

    if (ns_bytes_received < sizeof(response)) {
        perror("Failed to receive full response from naming server\n");
        return -1;
    }

    int ss_sockfd = connect_with_ip_port(response.ip, response.port);
    if (ss_sockfd < 0) {
        fprintf(stderr, "Failed to connect to storage server at %s:%d\n", response.ip, response.port);
        return -1;
    }
    fprintf(stderr, "Connected to storage server at %s:%d\n", response.ip, response.port);
    
    request(ss_sockfd, -1, READ, 0, (char *[]){(char *)filepath, NULL}, NULL, NULL);
    response_header ss_response;
    ssize_t ss_bytes_received = recv(ss_sockfd, &ss_response, sizeof(ss_response), MSG_WAITALL);
    response_to_string(&ss_response);
    if (ss_bytes_received < sizeof(ss_response)) {
        perror("Failed to receive full response from storage server\n");
        close(ss_sockfd);
        return -1;
    }

    uint64_t data_bytes_received = 0;
    while (data_bytes_received < ss_response.contentLength) {
        char data_buffer[BUFFER_SIZE];
        ssize_t n = recv(ss_sockfd, data_buffer, sizeof(data_buffer), 0);
        if (n < 0) {
            perror("Failed to receive data from storage server\n");
            close(ss_sockfd);
            return -1;
        }
        if (n == 0) break;
        fwrite(data_buffer, 1, n, stdout); // Print the received data
        data_bytes_received += n;
    }

    close(ss_sockfd);
    return 0;
}

int stream(const char * filepath){
    
    // char response[BUFFER_SIZE];
    // char request[BUFFER_SIZE];
    // if (send_it(3, 1, filepath, ns_socket) < 0){
    //     perror("Failed to send request to naming server.\n");
    //     return -1;
    // }

    // ssize_t ns_bytes_received;

    // ns_bytes_received = recv_full(ns_socket, response, 30, 0);
    
    
    // if (ns_bytes_received <  30) {
    //     perror("Failed to receive response from naming server.\n");
    //     return -1;
    // }
    // response[ns_bytes_received] = '\0';
    // char content_length[20];
    // memset(content_length, 0, sizeof(content_length));
    // strncpy(content_length, &response[10], 20);

    // if (atoi(content_length) < 0){
    //     printf("Sorry, the file was not found or there was some other error in the (naming) server.\n");
    //     return -1;
    // }
    

    // char reqid[9];
    // memset(reqid, 0, sizeof(reqid));
    // strncpy(reqid, &response[1], 9);
    // int req_id = atoi(reqid);

    // ns_bytes_received = recv_full(ns_socket, response, atoi(content_length), 0);
    // response[ns_bytes_received] = '\0';

    // if (ns_bytes_received != atoi(content_length)) {
    //     perror("Failed to receive response from naming server.\n");
    //     return -1;
    // }


    
    // char * ss_ip;
    // int ss_portnum;
    // char * saveptr;

    // ss_ip = strtok_r(response, "\n", &saveptr);
    // char *port_str = strtok_r(NULL, "\n", &saveptr);

    // if (!ss_ip || !port_str) {
    //     fprintf(stderr, "\"%s\": response received from naming server.\n", response);
    //     return -1;
    // }
    // ss_portnum = atoi(port_str);     // Convert port string to integer


    // // Check if is less than 0
    // if (ss_portnum < 0) {
    //     printf("Sorry, the file was not found.\n");
    //     return -1;
    // }


    // int ss_socket;
    // struct sockaddr_in server_addr;

    // // Create the socket
    // ss_socket = socket(AF_INET, SOCK_STREAM, 0);
    // if (ss_socket < 0) {
    //     perror("Failed to create socket for storage server\n");
    //     return -1;
    // }

    // // Set up the server address structure
    // memset(&server_addr, 0, sizeof(server_addr));
    // server_addr.sin_family = AF_INET;
    // server_addr.sin_port = htons(ss_portnum);

    // // Convert the IP address from text to binary form
    // if (inet_pton(AF_INET, ss_ip, &server_addr.sin_addr) <= 0) {
    //     perror("Invalid IP address format for storage server\n");
    //     //close(ss_socket);
    //     return -1;
    // }

    // int attempt = 0;
    // while (attempt < TIMEOUT) {
    //     printf("Trying to connect to storage server...\n");
    //     if (connect(ss_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
    //         // printf("Connected to storage server at %s:%d\n", ss_ip, ss_portnum);
    //         break;
    //     }

    //     sleep(1); // Wait 1 second before retrying
    //     attempt++;
    //     if (attempt >= TIMEOUT){
    //         perror("Failed to connect to the storage server.\n");
    //         //close(ss_socket);
    //         return -1;
    //     }
    // }

    // char length_buffer[31]; // For the 20-byte length response + null terminator
    // char data_buffer[BUFFER_SIZE]; // To store the actual data
    // ssize_t ss_bytes_received = 0;
    // long long int data_length;

    // // Send the request to the server
    // if (send_it(1, req_id, filepath, ss_socket) < 0) {
    //     perror("Failed to send request to storage server\n");
    //     //close(ss_socket);
    //     return -1;
    // }

    // // Receive the 30-byte header
    // ss_bytes_received = recv_full(ss_socket, length_buffer, 30, 0);
    // if (ss_bytes_received < 30) {
    //     perror("Failed to receive correct response from storage server\n");
    //     //close(ss_socket);
    //     return -1;
    // }

    // length_buffer[ss_bytes_received] = '\0'; // Null-terminate the length buffer

    // data_length = atoi(&length_buffer[10]); // Convert the length to an integer



    // // Check if the length is negative
    // if (data_length < 0) {
    //     fprintf(stderr, "Error: Received negative data length\n"); //(%lld)\n", data_length
    //     //close(ss_socket);
    //     return -1;
    // }

    // ao_device *device;
    // ao_sample_format format;
    // int default_driver;

    // // Initialize the AO library
    // ao_initialize();

    // // Set up the sample format for 16-bit mono PCM data
    // format.bits = 16;
    // format.channels = 1;  // Mono output
    // format.rate = 44100;  // Sample rate (Hz)
    // format.byte_format = AO_FMT_LITTLE;

    // // Find the default audio driver
    // default_driver = ao_default_driver_id();

    // // Open the audio device
    // device = ao_open_live(default_driver, &format, NULL);
    // if (device == NULL) {
    //     fprintf(stderr, "Error: Could not open audio device\n");
    //     ao_shutdown();
    //     return -1;
    // }


    // ss_bytes_received = 0;
    // int num;

    // ss_bytes_received = 0;
    // while (ss_bytes_received < data_length) {
    //     int remaining = data_length - ss_bytes_received;
    //     int to_read = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
        
    //     num = recv_full(ss_socket, data_buffer, to_read, 0);
    //     if (num <= 0) {
    //         perror("Failed to receive audio data");
    //         break;
    //     }
        
    //     ao_play(device, data_buffer, num);
    //     ss_bytes_received += num;
    // }

    // // Close the audio device and shutdown the AO library
    // ao_close(device);
    // ao_shutdown();
    // //close(ss_socket); 
    return 0;
}


void * write_async(void *args) {
    // ThreadArgs *threadArgs = (ThreadArgs *)args;
    // FILE *f = threadArgs->file;
    // int ss_socket = threadArgs->ss_socket;

    // size_t bytesRead;
    // long long int totalbytesread = 0;
    // long long int fileSize;
    // char data_buffer[BUFFER_SIZE];
    // char ss_response[31];

    // // Calculate file size
    // fseek(f, 0, SEEK_END);
    // fileSize = ftell(f);
    // fseek(f, 0, SEEK_SET);

    // while ((bytesRead = fread(data_buffer, 1, BUFFER_SIZE, f)) > 0) {
    //     totalbytesread += bytesRead;

    //     if (totalbytesread == fileSize) {
    //         if (send(ss_socket, data_buffer, bytesRead, 0) < 0) {
    //             printf("Failed to send complete data to storage server with socket number %d. %lld bytes sent successfully.\n", ss_socket, totalbytesread - bytesRead);
    //             fclose(f);
    //             //close(ss_socket);
    //             free(threadArgs);
    //             return NULL;
    //         }
    //         printf("\nSuccess! Data wholly sent to storage server at socket %d!\n", ss_socket);
    //         if (recv_full(ss_socket, ss_response, 30, 0) < 30 || ss_response[0] != '0') {
    //             printf("However, storage server did not acknowledge the whole data being written.\n");
    //         } else {
    //             printf("Storage server at socket %d acknowledged the whole data being successfully written.\n", ss_socket);
    //         }
    //         break;
    //     }
    //     if (send(ss_socket, data_buffer, BUFFER_SIZE, 0) < 0) {
    //         printf("Failed to send complete data to storage server at socket %d. %lld bytes sent successfully.\n", ss_socket, totalbytesread - bytesRead);
    //         fclose(f);
    //         //close(ss_socket);
    //         free(threadArgs);
    //         return NULL;
    //     }
    // }

    // if (ferror(f)) {
    //     perror("Error reading file");
    // }

    // fclose(f);
    // //close(ss_socket);
    // free(threadArgs);
    return NULL;
}


int write_it(const char * sourcefilepath, const char * destfilepath, bool synchronous){


    // FILE * f = fopen(sourcefilepath, "rb");
    // if (f == NULL) {
    //     perror("Error opening file");
    //     return -1;
    // }

    // fclose(f);

    
    // char response[BUFFER_SIZE];
    // char request[BUFFER_SIZE];
    // if (send_it(2, 1, destfilepath, ns_socket) < 0){
    //     perror("Failed to send request to naming server.\n");
    //     return -1;
    // }

    // ssize_t ns_bytes_received;

    // ns_bytes_received = recv_full(ns_socket, response, 30, 0);
    // response[ns_bytes_received] = '\0';
    // fprintf(stderr, "%s\n", response);
    
    // if (ns_bytes_received < 30) {
    //     perror("Failed to receive response from naming server.\n");
    //     return -1;
    // }

    // char content_length[20];
    // memset(content_length, 0, sizeof(content_length));
    // strncpy(content_length, &response[10], 20);


    // if (atoi(content_length) < 0){
    //     printf("Sorry, the folder was not found or there was some other error in the (naming) server.\n");
    //     return -1;
    // }

    // char reqid[9];
    // memset(reqid, 0, sizeof(reqid));
    // strncpy(reqid, &response[1], 9);
    // int req_id = atoi(reqid);

    // ns_bytes_received = recv_full(ns_socket, response, atoi(content_length), 0);
    // response[ns_bytes_received] = '\0';
    // if (ns_bytes_received != atoi(content_length)) {
    //     perror("Failed to receive response from naming server.\n");
    //     return -1;
    // }

    // fprintf(stderr, "%s\n", response);

    // char * ss_ip;
    // int ss_portnum;
    // char * saveptr;

    // ss_ip = strtok_r(response, "\n", &saveptr);
    // char *port_str = strtok_r(NULL, "\n", &saveptr);

    // if (!ss_ip || !port_str) {
    //     fprintf(stderr, "\"%s\": response received from naming server.\n", response);
    //     return -1;
    // }
    // ss_portnum = atoi(port_str);     // Convert port string to integer

    // char * ss_ip2;
    // int ss_portnum2 = -1;

    // char * ss_ip3;
    // int ss_portnum3 = -1;
    
    // if(saveptr){
    //     ss_ip2 = NULL;
    //     ss_ip3 = NULL;
    //     ss_ip2 = strtok_r(response, "\n", &saveptr);
    //     port_str = strtok_r(NULL, "\n", &saveptr);



    //     if (port_str != NULL){
    //         ss_portnum2 = atoi(port_str);
    //     }

    //     if (saveptr){
    //         ss_ip3 = strtok_r(response, "\n", &saveptr);
    //         port_str = strtok_r(NULL, "\n", &saveptr);

    //         if (port_str != NULL){
    //             ss_portnum3 = atoi(port_str);
    //         }
    //     }

    // }
    // // Check if is less than 0
    // if (ss_portnum < 0) {
    //     printf("Sorry, the file was not found.\n");
    //     return -1;
    // }


    // int ss_socket;
    // struct sockaddr_in server_addr;

    // // Create the socket
    // ss_socket = socket(AF_INET, SOCK_STREAM, 0);
    // if (ss_socket < 0) {
    //     perror("Failed to create socket for storage server\n");
    //     return -1;
    // }

    // // Set up the server address structure
    // memset(&server_addr, 0, sizeof(server_addr));
    // server_addr.sin_family = AF_INET;
    // server_addr.sin_port = htons(ss_portnum);

    // // Convert the IP address from text to binary form
    // if (inet_pton(AF_INET, ss_ip, &server_addr.sin_addr) <= 0) {
    //     perror("Invalid IP address format for storage server\n");
    //     //close(ss_socket);
    //     return -1;
    // }

    // int attempt = 0;
    // while (attempt < TIMEOUT) {
    //     printf("Trying to connect to storage server...\n");
    //     if (connect(ss_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
    //         // printf("Connected to storage server at %s:%d\n", ss_ip, ss_portnum);
    //         break;
    //     }

    //     sleep(1); // Wait 1 second before retrying
    //     attempt++;
    //     if (attempt >= TIMEOUT){
    //         perror("Failed to connect to the storage server.\n");
    //         //close(ss_socket);
    //         return -1;
    //     }
    // }

    // char length_buffer[31]; // For the 20-byte length response + null terminator
    // char data_buffer[BUFFER_SIZE]; // To store the actual data
    // ssize_t ss_bytes_received = 0;
    // long long int data_length;
    // char * destfilepath2 = (char *) malloc(sizeof(char) * (strlen(destfilepath) + 100));

    // strcpy(&destfilepath2[strlen(destfilepath2)], destfilepath);
    // destfilepath2[strlen(destfilepath2)] = '\n';
    // if(ss_ip2 != NULL && ss_ip3 != NULL){
    //     sprintf(&destfilepath2[strlen(destfilepath2)],"%s\n%d\n%s\n%d\n", ss_ip2, ss_portnum2, ss_ip3, ss_portnum3);        
    // }
    // else if(ss_ip2 != NULL){
    //     sprintf(&destfilepath2[strlen(destfilepath2)], "%s\n%d\n127.0.0.1\n-1\n", ss_ip2, ss_portnum2);
    // }
    // else{
    //     sprintf(&destfilepath2[strlen(destfilepath2)], "127.0.0.1\n-1\n127.0.0.1\n-1\n");
    // }

    // destfilepath2[strlen(destfilepath2)] = '\0';

    // fprintf(stderr, "%sNEWLINE\n", destfilepath2);

    // f = fopen(sourcefilepath, "rb");
    // if (f == NULL) {
    //     perror("Error opening file");
    //     //close(ss_socket);
    //     return -1;
    // }

    // // Seek to the end of the file
    // fseek(f, 0, SEEK_END);
    // long fileSize = ftell(f); // Get the current position in the file (size in bytes)
    // rewind(f);

    // if (fileSize == -1L) {
    //     perror("Error determining file size");
    //     //close(ss_socket);
    //     fclose(f);
    //     return -1;
    // }


    // if(long_send_it(2, req_id, destfilepath2, ss_socket, strlen(destfilepath2) + fileSize) < 0){
    //     printf("Failed to send request to storage server.\n");
    // }

    // char ss_response[31];
    // memset(ss_response, 0, sizeof(ss_response));
    // if (recv_full(ss_socket, ss_response, 30, 0) < 0){
    //     printf("Failed to receive response from storage server.\n");
    //     //close(ss_socket);
    //     fclose(f);
    //     return -1;
    // }

    // ss_response[30] = '\0';

    // if (ss_response[0] != '1'){
    //     printf("Storage server did not accept the request.\n");
    //     //close(ss_socket);
    //     fclose(f);
    //     return -1;        
    // }
    // // now, ACK 1 has been received from SS
    // // request acknowledged, we can begin writing
    // if (synchronous || fileSize <= ASYNC_SIZE){

    //     size_t bytesRead;
    //     long long int totalbytesread = 0;

    //     while ((bytesRead = fread(data_buffer, 1, BUFFER_SIZE, f)) > 0) {
    //         totalbytesread += bytesRead;
    //         // Simulate transmission by writing to stdout

    //         if (totalbytesread == fileSize){
    //             if (send(ss_socket, data_buffer,  bytesRead, 0)<0){
    //                 printf("Failed to send complete data to storage server. %lld bytes sent successfully.\n", totalbytesread - bytesRead);
    //                 fclose(f);
    //                 //close(ss_socket);
    //                 return -1;
    //             }
    //             printf("\nSuccess! Data wholly sent!\n");
    //             if(recv_full(ss_socket, ss_response, 30, 0)< 30 || ss_response[0] > '1'){
    //                 printf("However, storage server did not acknowledge the whole data being written.\n");
    //             }
    //             else{
    //                 printf("Storage server acknowledged the whole data being successfully written.\n");
    //             }
    //             break;
    //         }
    //         if (send(ss_socket, data_buffer,  BUFFER_SIZE, 0)<0){
    //             printf("Failed to send complete data to storage server. %lld bytes sent successfully.\n", totalbytesread - bytesRead);
    //             fclose(f);
    //             //close(ss_socket);
    //             return -1;
    //         }
    //     }

    //     if (ferror(f)) {
    //         perror("Error reading file");
    //         fclose(f);
    //         //close(ss_socket);
    //         return -1;
    //     }

        

    //     fclose(f);
    //     //close(ss_socket);
    //     return 0;

    // }
    // else{// ASYNC write activated here

    //     ThreadArgs *threadArgs = malloc(sizeof(ThreadArgs));
    //     if (!threadArgs) {
    //         perror("Failed to allocate memory for thread arguments\n");
    //         fclose(f);
    //         return -1;
    //     }

    //     threadArgs->file = f;
    //     threadArgs->ss_socket = ss_socket;

    //     pthread_t thread_id;
    //     if (pthread_create(&thread_id, NULL, write_async, threadArgs) != 0) {
    //         perror("Failed to create thread\n");
    //         free(threadArgs);
    //         //close(ss_socket);
    //         fclose(f);
    //         return -1;
    //     }
    //     printf("Asynchronous write was successfully activated because the file was too large. Your request with reqid %d is being processed at ss_socket %d\n", req_id, ss_socket);

    //     // Detach the thread so it runs independently
    //     pthread_detach(thread_id);

    //     return 0;
    // }
    return 0;
}

// accepted operations: 
//READ filepath
//WRITE sourcefilepath destfilepath
//CREATE folderpath (name) (name can be file or folder)
//DELETE folderpath (name) (name can be file or folder)
//COPY sourcepath destpath (assumes only for files)
//INFO filepath (filesize, last modified time, whatever)
//STREAM audiofilepath
//LIST folderpath
// flag --SYNC will be considered too, ASSUMPTION: it must be at the end
// ASSUMPTION only write can be asynchronous

void help(){
    printf("Available commands:\n");
    printf("CREATE <folderpath> <folderpath/filepath>\nREAD <filepath>\nWRITE <localsourcefilepath> <destfilepath>\nDELETE <folderpath> <folderpath/filepath>\nCOPY <sourcepath(file/folder)> <destpath(file/folder)>\nINFO <filepath>\nSTREAM <audiofilepath>\nSTOP\nHELP\nThese are the available commands\n");
    return;
}

int main(int argc, char* argv[]) {
    char request[BUFFER_SIZE];
    bool synchronous;

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <ip> <port>", argv[0]);
        exit(1);
    }

    ns_socket  = connect_with_ip_port(argv[1], atoi(argv[2]));
    if (ns_socket < 0){
        perror("Failed to connect to naming server");
        exit(1);
    }

    while(1){
        
        synchronous = false;
        if (fgets(request, sizeof(request), stdin) == NULL) {
            printf("Error reading input\n");
            continue;
        }

        if (strchr(request, '\n') == NULL) {
            // Flush remaining characters if input exceeded buffer size
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF);  // discard the rest of the line
            printf("Sorry, input exceeded buffer size.\n");
            continue;
        } 
        if (request[0] == '\n') {
            continue;
        }

        // Check if --SYNC flag is present
        if (strstr(request, "--SYNC") != NULL) {
            synchronous = true;
            // Remove --SYNC from the request for easier parsing
            char *sync_pos = strstr(request, "--SYNC");
            *sync_pos = '\0';  // Null-terminate to remove the flag from the command string
        }

        // Split command and arguments
        char operation[50], arg1[PATH_MAX], arg2[PATH_MAX];
        int num_args = sscanf(request, "%49s %4096s %4096s", operation, arg1, arg2);
        // printf("%s\n%s\n%s IS OVER", operation, arg1, arg2);

        // Determine the operation and call the corresponding function
        if (strcmp(operation, "WRITE") == 0 && num_args == 3) {
            write_it(arg1, arg2, synchronous);
        } 
        else if (strcmp(operation, "READ") == 0 && num_args == 2) {
            read_it(arg1); 
        } 
        else if (strcmp(operation, "INFO") == 0 && num_args == 2) {
            info(arg1);
        }
        else if (strcmp(operation, "LIST") == 0 && num_args == 2) {
            list(arg1);
        }
        else if (strcmp(operation, "STREAM") == 0 && num_args == 2) {
            stream(arg1);
        } 

        else if (strcmp(operation, "CREATE") == 0 && num_args == 3) {
            create(arg1, arg2);
        } 
        else if (strcmp(operation, "DELETE") == 0 && num_args == 3) {
            delete(arg1, arg2);
        } 
        else if (strcmp(operation, "COPY") == 0 && num_args == 3) {
            copy(arg1, arg2);
        } 
        else if (strcmp(operation, "HELP") == 0 && num_args == 1) {
            help();
        }
        else if (strcmp(operation, "STOP") == 0 && num_args == 1) {
            //(close_ns_socket);
            printf("Goodbye!\n");
            exit(0);
        }
        else {
            printf("ERROR: Invalid operation or incorrect arguments. For the format and available operations, use the command \"HELP\".\n");
        }
    }
    
    return 0;
}

