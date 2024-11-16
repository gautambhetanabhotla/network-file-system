#include <stdio.h>
#include <stdbool.h>
#define TIMEOUT 5000 // TIMEOUT in milliseconds, if ACK is not received in TIMEOUT 
#define BUFFER_SIZE 1000
#define FILEPATH_SIZE 300

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
int main(int argc, char* argv[]) {
    char request[BUFFER_SIZE];
    bool synchronous;
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
        int num_args = sscanf(request, "%49s %299s %299s", operation, arg1, arg2);

        // Determine the operation and call the corresponding function
        if (strcmp(operation, "WRITE") == 0 && num_args == 3) {
            write(arg1, arg2, synchronous);
        } 
        else if (strcmp(operation, "READ") == 0 && num_args == 2) {
            read(arg1);
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
            delete(arg1, arg2, synchronous);
        } 
        else if (strcmp(operation, "COPY") == 0 && num_args == 3) {
            copy(arg1, arg2, synchronous);
        } 

        else {
            printf("ERROR: Invalid operation or incorrect arguments.\n");
        }
    }   

    
    return 0;
}