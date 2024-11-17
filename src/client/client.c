#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <ao/ao.h>
#define TIMEOUT 5 // TIMEOUT in seconds, if ACK is not received in TIMEOUT 
#define FILEPATH_SIZE 4096
#define BUFFER_SIZE (2 * FILEPATH_SIZE + 200)



int send_it(const int op_id, const int req_id, const char * content, const int socket){
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
    snprintf(request, sizeof(request), "%c%s%s%s", op, reqid, content_length, content);
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
            send(ns_socket, "CLIENT\0\0\0\0\0\0\0", 13);
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


int ss_connect(const char *server_ip, int server_port, int * ss_socket){
    struct sockaddr_in server_address;

    // Create socket
    (*ss_socket) = socket(AF_INET, SOCK_STREAM, 0);
    if ((*ss_socket) < 0) {
        perror("Storage server socket creation failed");
        return -1;
    }

    // Set server address details
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port);

    // Convert and set the IP address
    if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) <= 0) {
        perror("Invalid storage server address/ address not supported");
        close((*ss_socket));
        return -1;
    }

    if (connect((*ss_socket), (struct sockaddr *)&server_address, sizeof(server_address)) == 0) {
        // printf("Connected to storage server at %s:%d\n", server_ip, server_port);
        return 0;
    }

    close((*ss_socket));
    (*ss_socket) = -1; // Reset the socket to indicate failure
    return -1;
    
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
    bytes_received = recv(ns_socket, buffer, sizeof(buffer) - 1, 0);

    while ((bytes_received = recv(ns_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0'; // Null-terminate the buffer
        printf("%s", buffer);          // Print the received chunk
    }

    if (bytes_received < 0) {
        perror("Failed to receive response from naming server.\n");
        return -1;
    }

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


int ns_request(const char * request, char * response, int response_size) {
    
    ssize_t bytes_received;

    // Send the request
    if (send(ns_socket, request, strlen(request), 0) < 0) {
        perror("Failed to send request to naming server.\n");
        return -1;
    }

    // printf("Response:\n");

    // Receive and print the entire response in chunks  
    bytes_received = recv(ns_socket, response, response_size, 0);
    response[bytes_received] = '\0';
    
    
    if (bytes_received < 0) {
        perror("Failed to receive response from naming server.\n");
        return -1;
    }
    // printf("\n"); // Ensure proper formatting after response
    return 0;
}

int read_it(const char * filepath){
    
    char response[BUFFER_SIZE];
    char request[BUFFER_SIZE];
    if (send_it(1, 1, filepath, ns_socket) < 0){
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
    if (send_it(1, req_id, filepath, ss_socket) < 0) {
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
        fprintf(stderr, "Error: Received negative data length\n"); //(%lld)\n", data_length
        close(ss_socket);
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
    if (send_it(1, 1, filepath, ns_socket) < 0){
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
    if (send_it(1, req_id, filepath, ss_socket) < 0) {
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




int write_it(const char * sourcerfilepath, const char * destfilepath, bool synchronous){
    
    char response[BUFFER_SIZE];
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "WRITE\n%s\n", destfilepath);

    if (ns_request(request, response, BUFFER_SIZE)< 0){
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
            printf("Connected to storage server at %s:%d\n", ss_ip, ss_portnum);
            break;
        }

        sleep(1); // Wait 1 second before retrying
        attempt++;
        if (attempt >= TIMEOUT){
            perror("Failed to connect to the storage server\n");
            close(ss_socket);
            return -1;
        }
    }



    char ss_request[5000];
    memset(ss_request, '\0', 5000);

    // Copy the filepath into the buffer starting at the 11th byte (index 10)
    strncpy(ss_request + 10, destfilepath, 4095); // Leave space for null termination
    ss_request[10 + 4095] = '\0';            // Ensure null termination if filepath is too long

    // Fill the next 20 bytes with '0' (starting from 10 + 4096)
    memset(ss_request + 10 + 4096, '0', 20); 



}

int main(int argc, char* argv[]) {
    char request[BUFFER_SIZE];
    bool synchronous;

    char serverip[] = "";
    int serverport = 8080;

    int check  = ns_connect(serverip, serverport);
    if (check < 0){
        printf("FAILED TO CONNECT WITH NAMING SERVER\n");
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
        else {
            printf("ERROR: Invalid operation or incorrect arguments.\n");
        }
    }   

    
    return 0;
}

