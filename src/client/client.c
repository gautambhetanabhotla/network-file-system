#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <ao/ao.h>
#include <pthread.h>
#define TIMEOUT 5 // TIMEOUT in seconds, if ACK is not received in TIMEOUT 
#define FILEPATH_SIZE 4096
#define BUFFER_SIZE (2 * FILEPATH_SIZE + 200)
#define ASYNC_SIZE 2000 // if it >= ASYNC_SIZE, it will be sent asynchronously if there is no --SYNC flag

typedef struct {
    FILE *file;
    int ss_socket;
} ThreadArgs;


int send_it(const int op_id, const int req_id, const char * ncontent, const int socket){

    char * content = (char *) malloc (strlen(ncontent) + 2);
    strcpy(content, ncontent);
    if(content[strlen(content) - 1] != '\n' && strlen(ncontent) > 0){
        content[strlen(content)] = '\n';      
        content[strlen(content) + 1] = '\0';
    }
    else{
        content[strlen(content)] = '\0';
    }
    

    if (op_id < 0 || op_id > 9 || socket <= 0){
        return -1;
    }


    char op = '0' + op_id;
    char reqid[9];
    memset(reqid, 0, sizeof(reqid));
    snprintf(reqid, sizeof(reqid), "%d", req_id);
    char content_length[20];
    memset(content_length, 0, sizeof(content_length));
    snprintf(content_length, sizeof(content_length), "%ld", strlen(content));
    char request[BUFFER_SIZE];
    memset(request, 0, sizeof(request));
    request[0]  = op;
    strncpy(&request[1], reqid, strlen(reqid));
    strncpy(&request[10], content_length, strlen(content_length));
    strncpy(&request[30], content, strlen(content));
    // snprintf(request, sizeof(request), "%c%s%s%s", op, reqid, content_length, content);
    // printf("REQUEST IS\n");
    // for(int i = 0; i<30; i++){
    //     printf("%c ", request[i]);
    // }
    // printf("\n");
    // printf("content is %s\n", content);
    if(send(socket, request, 30 + strlen(content), 0) < 0){
        return -1;
    } 
    return 0;
}

int long_send_it(const int op_id, const int req_id, const char * ncontent, const int socket, long long content_size){
    if (op_id < 0 || op_id > 9 || socket <= 0){
        return -1;
    }

    char * content = (char *) malloc (strlen(ncontent) + 2);
    strcpy(content, ncontent);
    
    if(content[strlen(content) - 1] != '\n' && strlen(ncontent) > 0){
        content[strlen(content)] = '\n';      
        content[strlen(content) + 1] = '\0';
    }
    else{
        content[strlen(content)] = '\0';
    }
    

    if (op_id < 0 || op_id > 9 || socket <= 0){
        return -1;
    }


    char op = '0' + op_id;
    char reqid[9];
    memset(reqid, 0, sizeof(reqid));
    snprintf(reqid, sizeof(reqid), "%d", req_id);
    char content_length[20];
    memset(content_length, 0, sizeof(content_length));
    snprintf(content_length, sizeof(content_length), "%lld", content_size);
    char request[BUFFER_SIZE];
    request[0]  = op;
    strncpy(&request[1], reqid, strlen(reqid));
    strncpy(&request[10], content_length, strlen(content_length));
    strncpy(&request[30], content, strlen(content));
    // snprintf(request, sizeof(request), "%c%s%s%s", op, reqid, content_length, content);
    if(send(socket, request, 30 + strlen(content), 0) < 0){
        return -1;
    } 
    return 0;
}


int ns_socket; // scoket for the naming server

int ns_connect(const char *server_ip, int server_port) {
    struct sockaddr_in server_address;
    int attempt = 0;

    // Create socket
    ns_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ns_socket < 0) {
        perror("Naming server socket creation failed");
        return -1;
    }

    // Set server address details
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port);

    // Convert and set the IP address
    if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) <= 0) {
        perror("Invalid naming server address/ address not supported");
        close(ns_socket);
        return -1;
    }

    // Attempt to connect with retry logic
    while (attempt < TIMEOUT) {
        printf("Trying to connect...\n");
        if (connect(ns_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == 0) {
            // if (send(ns_socket, "CLIENT\0\0\0\0\0\0\0", 13,0) < 0){
            //     break;
            // }
            printf("Connected to naming server at %s:%d\n", server_ip, server_port);
            return 0;
        }

        sleep(1); // Wait 1 second before retrying
        attempt++;
    }

    printf("Connection failed. Exiting...\n");
    close(ns_socket);
    ns_socket = -1; // Reset the socket to indicate failure
    return -1;
}


// int ss_connect(const char *server_ip, int server_port, int * ss_socket){
//     struct sockaddr_in server_address;

//     // Create socket
//     (*ss_socket) = socket(AF_INET, SOCK_STREAM, 0);
//     if ((*ss_socket) < 0) {
//         perror("Storage server socket creation failed");
//         return -1;
//     }

//     // Set server address details
//     memset(&server_address, 0, sizeof(server_address));
//     server_address.sin_family = AF_INET;
//     server_address.sin_port = htons(server_port);

//     // Convert and set the IP address
//     if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) <= 0) {
//         perror("Invalid storage server address/ address not supported");
//         close((*ss_socket));
//         return -1;
//     }

//     if (connect((*ss_socket), (struct sockaddr *)&server_address, sizeof(server_address)) == 0) {
//         // printf("Connected to storage server at %s:%d\n", server_ip, server_port);
//         return 0;
//     }

//     close((*ss_socket));
//     (*ss_socket) = -1; // Reset the socket to indicate failure
//     return -1;
    
// }

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

int ns_request_print(int op_id, char * content) {
    int req_id = 1;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    // Send the request
    if (send_it(op_id, req_id, content, ns_socket) < 0) {
        perror("Failed to send request to naming server.\n");
        return -1;
    }

    // printf("Response:\n");

    // Receive and print the entire response in chunks
    bytes_received = recv(ns_socket, buffer, 30, 0);
    buffer[bytes_received] = '\0'; // Null-terminate the buffer
    printf("HEADER IS %s\n", buffer);
    int content_length = atoi(&buffer[10]);
    while(content_length){
        bytes_received = recv(ns_socket, buffer, content_length % (sizeof(buffer) - 1), 0);
        if (bytes_received < 0){
            printf("Failed to receive response from naming server.\n");
            return -1;
        }
        content_length -= bytes_received;
        buffer[bytes_received] = '\0'; // Null-terminate the buffer
        printf("%s", buffer);          // Print the received chunk

    }


    // while ((bytes_received = recv(ns_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
    //     buffer[bytes_received] = '\0'; // Null-terminate the buffer
    //     printf("%s", buffer);          // Print the received chunk
    // }

    // if (bytes_received < 0) {
    //     perror("Failed to receive response from naming server.\n");
    //     return -1;
    // }

    printf("\n"); // Ensure proper formatting after response
    return 0;
}

// CREATE operation
int create(const char *folderpath, const char *fpath) {
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "%s\n%s\n", folderpath, fpath);
    return ns_request_print(6, request);
}

// DELETE operation
int delete(const char *folderpath, const char *fpath) {
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "%s\n%s\n", folderpath, fpath);
    return ns_request_print(8, request);
}

// COPY operation
int copy(const char *source_filepath, const char *dest_folderpath) {
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "%s\n%s\n", source_filepath, dest_folderpath);
    return ns_request_print(7, request);
}

// INFO operation
int info(const char *filepath) {
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "%s\n", filepath);
    return ns_request_print(4, request);
}

// LIST operation
int list(const char *folderpath) {
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "%s\n", folderpath);
    return ns_request_print(5, request);
}


// int ns_request(const char * request, char * response, int response_size) {
    
//     ssize_t bytes_received;

//     // Send the request
//     if (send(ns_socket, request, strlen(request), 0) < 0) {
//         perror("Failed to send request to naming server.\n");
//         return -1;
//     }

//     // printf("Response:\n");

//     // Receive and print the entire response in chunks  
//     bytes_received = recv(ns_socket, response, response_size, 0);
//     response[bytes_received] = '\0';
    
    
//     if (bytes_received < 0) {
//         perror("Failed to receive response from naming server.\n");
//         return -1;
//     }
//     // printf("\n"); // Ensure proper formatting after response
//     return 0;
// }

int read_it(const char * filepath){
    
    char response[BUFFER_SIZE];
    char request[BUFFER_SIZE];
    if (send_it(1, 1, filepath, ns_socket) < 0){
        perror("Failed to send request to naming server.\n");
        return -1;
    }

    ssize_t ns_bytes_received;

    ns_bytes_received = recv(ns_socket, response, 30, 0);
    
    
    if (ns_bytes_received != 30) {
        perror("Failed to receive response from naming server.\n");
        return -1;
    }
    response[ns_bytes_received] = '\0';
    char content_length[20];
    memset(content_length, 0, sizeof(content_length));
    strncpy(content_length, &response[10], 20);

    if (atoi(content_length) < 0){
        printf("Sorry, the file was not found or there was some other error in the (naming) server.\n");
        return -1;
    }
    

    char reqid[9];
    memset(reqid, 0, sizeof(reqid));
    strncpy(reqid, &response[1], 9);
    int req_id = atoi(reqid);

    ns_bytes_received = recv(ns_socket, response, atoi(content_length), 0);
    response[ns_bytes_received] = '\0';

    if (ns_bytes_received != atoi(content_length)) {
        perror("Failed to receive response from naming server.\n");
        return -1;
    }


    
    char * ss_ip;
    int ss_portnum;
    char * saveptr;

    ss_ip = strtok_r(response, "\n", &saveptr);
    char *port_str = strtok_r(NULL, "\n", &saveptr);

    if (!ss_ip || !port_str) {
        fprintf(stderr, "Invalid response received from naming server.\n");
        return -1;
    }
    ss_portnum = atoi(port_str);     // Convert port string to integer


    // Check if is less than 0
    if (ss_portnum < 0) {
        printf("Sorry, the file was not found.\n");
        return -1;
    }


    int ss_socket;
    struct sockaddr_in server_addr;

    // Create the socket
    ss_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_socket < 0) {
        perror("Failed to create socket for storage server\n");
        return -1;
    }

    // Set up the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ss_portnum);

    // Convert the IP address from text to binary form
    if (inet_pton(AF_INET, ss_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid IP address format for storage server\n");
        close(ss_socket);
        return -1;
    }

    int attempt = 0;
    while (attempt < TIMEOUT) {
        printf("Trying to connect to storage server...\n");
        if (connect(ss_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
            // printf("Connected to storage server at %s:%d\n", ss_ip, ss_portnum);
            break;
        }

        sleep(1); // Wait 1 second before retrying
        attempt++;
        if (attempt >= TIMEOUT){
            perror("Failed to connect to the storage server.\n");
            close(ss_socket);
            return -1;
        }
    }

    char length_buffer[31]; // For the 20-byte length response + null terminator
    char data_buffer[BUFFER_SIZE]; // To store the actual data
    ssize_t ss_bytes_received = 0;
    long long int data_length;

    // Send the request to the server
    if (send_it(1, req_id, filepath, ss_socket) < 0) {
        perror("Failed to send request to storage server\n");
        close(ss_socket);
        return -1;
    }

    // Receive the 30-byte header
    ss_bytes_received = recv(ss_socket, length_buffer, 30, 0);
    if (ss_bytes_received != 30) {
        perror("Failed to receive correct response from storage server\n");
        close(ss_socket);
        return -1;
    }

    length_buffer[ss_bytes_received] = '\0'; // Null-terminate the length buffer

    data_length = atoi(&length_buffer[10]); // Convert the length to an integer



    // Check if the length is negative
    if (data_length < 0) {
        fprintf(stderr, "Error: Received negative data length\n"); //(%lld)\n", data_length
        close(ss_socket);
        return -1;
    }

    ss_bytes_received = 0;
    int num;
    while(1){
        // Receive the actual data
        num = recv(ss_socket, data_buffer, (data_length - ss_bytes_received) % (BUFFER_SIZE - 1), 0);
        if (num < 0){
            perror("Failed to receive complete data\n");
            break;
        }
        if (num == 0){
            perror("Failed to receive complete data, connection with storage server terminated.\n");
            break;
        }
        ss_bytes_received += num;
        if (ss_bytes_received == data_length) {
            data_buffer[num] = '\0';
            printf("%s", data_buffer);
            printf("\nSuccess! Data read wholly!\n");
            break;
        }
        printf("%s", data_buffer);
    }


    close(ss_socket); 
    return 0;
}


int stream(const char * filepath){
    
    char response[BUFFER_SIZE];
    char request[BUFFER_SIZE];
    if (send_it(3, 1, filepath, ns_socket) < 0){
        perror("Failed to send request to naming server.\n");
        return -1;
    }

    ssize_t ns_bytes_received;

    ns_bytes_received = recv(ns_socket, response, BUFFER_SIZE, 0);
    response[ns_bytes_received] = '\0';
    
    
    if (ns_bytes_received < 30) {
        perror("Failed to receive response from naming server.\n");
        return -1;
    }
    char content_length[20];
    memset(content_length, 0, sizeof(content_length));
    strncpy(content_length, &response[10], 20);

    if (atoi(content_length) < 0){
        printf("Sorry, the file was not found or there was some other error in the (naming) server.\n");
        return -1;
    }

    char reqid[9];
    memset(reqid, 0, sizeof(reqid));
    strncpy(reqid, &response[1], 9);
    int req_id = atoi(reqid);


    
    char * ss_ip;
    int ss_portnum;
    char * saveptr;

    ss_ip = strtok_r(&response[30], "\n", &saveptr);
    char *port_str = strtok_r(NULL, "\n", &saveptr);

    if (!ss_ip || !port_str) {
        fprintf(stderr, "Invalid response received from naming server.\n");
        return -1;
    }
    ss_portnum = atoi(port_str);     // Convert port string to integer


    // Check if is less than 0
    if (ss_portnum < 0) {
        printf("Sorry, the file was not found.\n");
        return -1;
    }


    int ss_socket;
    struct sockaddr_in server_addr;

    // Create the socket
    ss_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_socket < 0) {
        perror("Failed to create socket for storage server\n");
        return -1;
    }

    // Set up the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ss_portnum);

    // Convert the IP address from text to binary form
    if (inet_pton(AF_INET, ss_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid IP address format for storage server\n");
        close(ss_socket);
        return -1;
    }

    int attempt = 0;
    while (attempt < TIMEOUT) {
        printf("Trying to connect to storage server...\n");
        if (connect(ss_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
            // printf("Connected to storage server at %s:%d\n", ss_ip, ss_portnum);
            break;
        }

        sleep(1); // Wait 1 second before retrying
        attempt++;
        if (attempt >= TIMEOUT){
            perror("Failed to connect to the storage server.\n");
            close(ss_socket);
            return -1;
        }
    }

    char length_buffer[31]; // For the 20-byte length response + null terminator
    char data_buffer[BUFFER_SIZE]; // To store the actual data
    ssize_t ss_bytes_received = 0;
    long long int data_length;

    // Send the request to the server
    if (send_it(3, req_id, filepath, ss_socket) < 0) {
        perror("Failed to send request to storage server\n");
        close(ss_socket);
        return -1;
    }

    // Receive the 30-byte header
    ss_bytes_received = recv(ss_socket, length_buffer, 30, 0);
    if (ss_bytes_received < 30) {
        perror("Failed to receive correct response from storage server\n");
        close(ss_socket);
        return -1;
    }

    length_buffer[strlen(length_buffer)] = '\0'; // Null-terminate the length buffer

    data_length = atoi(&length_buffer[10]); // Convert the length to an integer



    // Check if the length is negative
    if (data_length < 0) {
        fprintf(stderr, "Error: Received negative data length\n");//(%lld) , data_length)
        close(ss_socket);
        return -1;
    }


    ao_device *device;
    ao_sample_format format;
    int default_driver;

    // Initialize the AO library
    ao_initialize();

    // Set up the sample format for 16-bit mono PCM data
    format.bits = 16;
    format.channels = 1;  // Mono output
    format.rate = 44100;  // Sample rate (Hz)
    format.byte_format = AO_FMT_LITTLE;

    // Find the default audio driver
    default_driver = ao_default_driver_id();

    // Open the audio device
    device = ao_open_live(default_driver, &format, NULL);
    if (device == NULL) {
        fprintf(stderr, "Error: Could not open audio device\n");
        ao_shutdown();
        return -1;
    }


    ss_bytes_received = 0;
    int num;
    while(1){
        // Receive the actual data
        num = recv(ss_socket, data_buffer, BUFFER_SIZE - 1, 0);
        if (num < 0){
            perror("Failed to receive complete data\n");
            break;
        }
        if (num == 0){
            perror("Failed to receive complete data, connection with storage server terminated.\n");
            break;
        }
        ss_bytes_received += num;
        if (ss_bytes_received == data_length) {
            memset(&data_buffer[num], '\0', BUFFER_SIZE - num);
            ao_play(device, data_buffer, num);
            printf("\nSuccess! Data streamed wholly!\n");
            break;
        }
        ao_play(device, data_buffer, num);
    }

    // Close the audio device and shutdown the AO library
    ao_close(device);
    ao_shutdown();
    close(ss_socket); 
    return 0;
}



void * write_async(void *args) {
    ThreadArgs *threadArgs = (ThreadArgs *)args;
    FILE *f = threadArgs->file;
    int ss_socket = threadArgs->ss_socket;

    size_t bytesRead;
    long long int totalbytesread = 0;
    long long int fileSize;
    char data_buffer[BUFFER_SIZE];
    char ss_response[31];

    // Calculate file size
    fseek(f, 0, SEEK_END);
    fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    while ((bytesRead = fread(data_buffer, 1, BUFFER_SIZE, f)) > 0) {
        totalbytesread += bytesRead;

        if (totalbytesread == fileSize) {
            if (send(ss_socket, data_buffer, bytesRead, 0) < 0) {
                printf("Failed to send complete data to storage server with socket number %d. %lld bytes sent successfully.\n", ss_socket, totalbytesread - bytesRead);
                fclose(f);
                close(ss_socket);
                free(threadArgs);
                return NULL;
            }
            printf("\nSuccess! Data wholly sent to storage server at socket %d!\n", ss_socket);
            if (recv(ss_socket, ss_response, 30, 0) < 30 || ss_response[0] != '0') {
                printf("However, storage server did not acknowledge the whole data being written.\n");
            } else {
                printf("Storage server at socket %d acknowledged the whole data being successfully written.\n", ss_socket);
            }
            break;
        }
        if (send(ss_socket, data_buffer, BUFFER_SIZE, 0) < 0) {
            printf("Failed to send complete data to storage server at socket %d. %lld bytes sent successfully.\n", ss_socket, totalbytesread - bytesRead);
            fclose(f);
            close(ss_socket);
            free(threadArgs);
            return NULL;
        }
    }

    if (ferror(f)) {
        perror("Error reading file");
    }

    fclose(f);
    close(ss_socket);
    free(threadArgs);
    return NULL;
}


int write_it(const char * sourcefilepath, const char * destfilepath, bool synchronous){


    FILE * f = fopen(sourcefilepath, "rb");
    if (f == NULL) {
        perror("Error opening file");
        return -1;
    }

    fclose(f);

    
    char response[BUFFER_SIZE];
    char request[BUFFER_SIZE];
    if (send_it(2, 1, destfilepath, ns_socket) < 0){
        perror("Failed to send request to naming server.\n");
        return -1;
    }

    ssize_t ns_bytes_received;

    ns_bytes_received = recv(ns_socket, response, BUFFER_SIZE, 0);
    response[ns_bytes_received] = '\0';
    
    
    if (ns_bytes_received < 30) {
        perror("Failed to receive response from naming server.\n");
        return -1;
    }
    char content_length[20];
    memset(content_length, 0, sizeof(content_length));
    strncpy(content_length, &response[10], 20);

    if (atoi(content_length) < 0){
        printf("Sorry, the folder was not found or there was some other error in the (naming) server.\n");
        return -1;
    }

    char reqid[9];
    memset(reqid, 0, sizeof(reqid));
    strncpy(reqid, &response[1], 9);
    int req_id = atoi(reqid);


    
    char * ss_ip;
    int ss_portnum;
    char * saveptr;

    ss_ip = strtok_r(&response[30], "\n", &saveptr);
    char *port_str = strtok_r(NULL, "\n", &saveptr);

    if (!ss_ip || !port_str) {
        fprintf(stderr, "Invalid response received from naming server.\n");
        return -1;
    }
    ss_portnum = atoi(port_str);     // Convert port string to integer


    // Check if is less than 0
    if (ss_portnum < 0) {
        printf("Sorry, the file was not found.\n");
        return -1;
    }


    int ss_socket;
    struct sockaddr_in server_addr;

    // Create the socket
    ss_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_socket < 0) {
        perror("Failed to create socket for storage server\n");
        return -1;
    }

    // Set up the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ss_portnum);

    // Convert the IP address from text to binary form
    if (inet_pton(AF_INET, ss_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid IP address format for storage server\n");
        close(ss_socket);
        return -1;
    }

    int attempt = 0;
    while (attempt < TIMEOUT) {
        printf("Trying to connect to storage server...\n");
        if (connect(ss_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
            // printf("Connected to storage server at %s:%d\n", ss_ip, ss_portnum);
            break;
        }

        sleep(1); // Wait 1 second before retrying
        attempt++;
        if (attempt >= TIMEOUT){
            perror("Failed to connect to the storage server.\n");
            close(ss_socket);
            return -1;
        }
    }

    char length_buffer[31]; // For the 20-byte length response + null terminator
    char data_buffer[BUFFER_SIZE]; // To store the actual data
    ssize_t ss_bytes_received = 0;
    long long int data_length;
    char * destfilepath2 = (char *) malloc(sizeof(char) * (strlen(destfilepath) + 2));
    strcpy(destfilepath2, destfilepath);
    destfilepath2[strlen(destfilepath)] = '\n';
    destfilepath2[strlen(destfilepath) + 1] = '\0';



    f = fopen(sourcefilepath, "rb");
    if (f == NULL) {
        perror("Error opening file");
        close(ss_socket);
        return -1;
    }

    // Seek to the end of the file
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f); // Get the current position in the file (size in bytes)
    rewind(f);

    if (fileSize == -1L) {
        perror("Error determining file size");
        close(ss_socket);
        fclose(f);
        return -1;
    }


    if(long_send_it(2, req_id, destfilepath2, ss_socket, strlen(destfilepath2) + fileSize) < 0){
        printf("Failed to send request to storage server.\n");
    }

    char ss_response[31];
    memset(ss_response, 0, sizeof(ss_response));
    if (recv(ss_socket, ss_response, 30, 0) < 30){
        printf("Failed to receive response from storage server.\n");
        close(ss_socket);
        fclose(f);
        return -1;
    }

    ss_response[30] = '\0';

    if (ss_response[0] != '1'){
        printf("Storage server did not accept the request.\n");
        close(ss_socket);
        fclose(f);
        return -1;        
    }
    // now, ACK 1 has been received from SS
    // request acknowledged, we can begin writing
    if (synchronous || fileSize <= ASYNC_SIZE){

        size_t bytesRead;
        long long int totalbytesread = 0;

        while ((bytesRead = fread(data_buffer, 1, BUFFER_SIZE, f)) > 0) {
            totalbytesread += bytesRead;
            // Simulate transmission by writing to stdout

            if (totalbytesread == fileSize){
                if (send(ss_socket, data_buffer,  bytesRead, 0)<0){
                    printf("Failed to send complete data to storage server. %lld bytes sent successfully.\n", totalbytesread - bytesRead);
                    fclose(f);
                    close(ss_socket);
                    return -1;
                }
                printf("\nSuccess! Data wholly sent!\n");
                if(recv(ss_socket, ss_response, 30, 0)< 30 || ss_response[0] != '0'){
                    printf("However, storage server did not acknowledge the whole data being written.\n");
                }
                else{
                    printf("Storage server acknowledged the whole data being successfully written.\n");
                }
                break;
            }
            if (send(ss_socket, data_buffer,  BUFFER_SIZE, 0)<0){
                printf("Failed to send complete data to storage server. %lld bytes sent successfully.\n", totalbytesread - bytesRead);
                fclose(f);
                close(ss_socket);
                return -1;
            }
        }

        if (ferror(f)) {
            perror("Error reading file");
            fclose(f);
            close(ss_socket);
            return -1;
        }

        

        fclose(f);
        close(ss_socket);
        return 0;

    }
    else{// ASYNC write activated here

        ThreadArgs *threadArgs = malloc(sizeof(ThreadArgs));
        if (!threadArgs) {
            perror("Failed to allocate memory for thread arguments\n");
            fclose(f);
            return -1;
        }

        threadArgs->file = f;
        threadArgs->ss_socket = ss_socket;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, write_async, threadArgs) != 0) {
            perror("Failed to create thread\n");
            free(threadArgs);
            close(ss_socket);
            fclose(f);
            return -1;
        }
        printf("Asynchronous write was successfully activated because the file was too large. Your request with reqid %d is being processed at ss_socket %d\n", req_id, ss_socket);

        // Detach the thread so it runs independently
        pthread_detach(thread_id);

        return 0;
    }

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
    printf("CREATE <folderpath> <folderpath/filepath>\nREAD <filepath>\nWRITE <localsourcefilepath> <destfilepath>\nDELETE <folderpath> <folderpath/filepath>\nCOPY <sourcepath(file/folder)> <destpath(file/folder)>\nINFO <filepath>\nSTREAM <audiofilepath>\nSTOP\nHELP\n");
    return;
}

int main(int argc, char* argv[]) {
    char request[BUFFER_SIZE];
    bool synchronous;

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <ip> <port>", argv[0]);
        exit(1);
    }

    char* serverip = argv[1];
    int serverport = atoi(argv[2]);

    int check  = ns_connect(serverip, serverport);
    if (check < 0){
        printf("FAILED TO CONNECT WITH NAMING SERVER\n");
        exit(1);
    }
    while(1){


        int error = 0;
        socklen_t len = sizeof(error);

        if (getsockopt(ns_socket, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            // An error occurred
            printf("SORRY, ERROR WITH NS CONNECTION");
            exit(1);
        }

        if (error != 0) {
            // Socket has an error
            printf("SORRY, ERROR WITH NS CONNECTION");
            exit(1);
        }
        
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
        char operation[50], arg1[FILEPATH_SIZE], arg2[FILEPATH_SIZE];
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
            close(ns_socket);
            printf("Goodbye!\n");
            exit(0);
        }
        else {
            printf("ERROR: Invalid operation or incorrect arguments. For the format and available operations, use the command \"HELP\".\n");
        }
    }   

    
    return 0;
}

