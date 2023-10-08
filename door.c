#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 256 // Maximum buffer size for communication

// Struct definition for the shared memory structure representing a door
typedef struct {
    char status; // 'O' for open , 'C' for closed, 'o' for opening, 'c' for closing
    pthread_mutex_t mutex;
    pthread_cond_t cond_start;
    pthread_cond_t cond_end;
} shm_door;

// Helper function to receive data until a specific delimiter is encountered or max length reached
ssize_t recv_until(int sockfd, char *buf, char delimiter, size_t max_len) {
    ssize_t total_received = 0, received;
    char c;
    while (total_received < max_len - 1) {
        received = recv(sockfd, &c, 1, 0);
        if (received <= 0) {
            return received;
        }
        buf[total_received++] = c;
        if (c == delimiter) {
            break;
        }
    }
    buf[total_received] = '\0';  // Null terminate the string
    return total_received;
}

int main(int argc, char **argv) {
    // Checking correct number of arguments provided
    if(argc != 7) {
        fprintf(stderr, "Usage: {id} {address:port} {FAIL_SAFE | FAIL_SECURE} {shared memory path} {shared memory offset} {overseer address:port}\n");
        return 1;
    }

    // Parsing command line arguments
    int id = atoi(argv[1]);
    const char *bind_address = argv[2];
    const char *door_mode = argv[3];
    const char *shm_path = argv[4];
    off_t shm_offset = (off_t)atoi(argv[5]);
    const char *overseer_addr = argv[6];

    // Open shared memory
    int shm_fd = shm_open(shm_path, O_RDWR, 0);
    if(shm_fd == -1) {
        perror("shm_open()");
        return 1;
    }

    // Mapping the shared memory into our process's address space
    shm_door *shared = (shm_door *)mmap(NULL, sizeof(shm_door), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, shm_offset);
    if(shared == MAP_FAILED) {
        perror("mmap()");
        return 1;
    }

    // Setup TCP socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1) {
        perror("socket()");
        return 1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(atoi(strchr(bind_address, ':') + 1));
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // Binding to the specified address and port
    if(bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("bind()");
        return 1;
    }

    // Listening for incoming connections
    if(listen(sockfd, 5) == -1) {
        perror("listen()");
        return 1;
    }

    // Send initialisation message to overseer
    int overseer_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in overseerAddr;
    overseerAddr.sin_family = AF_INET;
    overseerAddr.sin_port = htons(atoi(strchr(overseer_addr, ':') + 1));
    overseerAddr.sin_addr.s_addr = inet_addr(overseer_addr);

    connect(overseer_fd, (struct sockaddr*)&overseerAddr, sizeof(overseerAddr));

    char message[BUFFER_SIZE];
    snprintf(message, sizeof(message), "DOOR %d %s %s#", id, bind_address, door_mode);
    send(overseer_fd, message, strlen(message), 0);
    close(overseer_fd);


    // Main loop to handle incoming connections and commands
    while(1) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int client_fd = accept(sockfd, (struct sockaddr*)&clientAddr, &clientLen);

        char buffer[BUFFER_SIZE];
        if (recv_until(client_fd, buffer, '#', sizeof(buffer)) <= 0) {
            close(client_fd);
            continue;
        }

        // Locking the mutex for synchronized access to door status
        pthread_mutex_lock(&shared->mutex);

        // Handling various door commands (OPEN, CLOSE, EMERGENCY OPEN, SECURE CLOSE)
        if(strcmp(buffer, "OPEN#") == 0) {
            if(shared->status == 'O') {
                send(client_fd, "ALREADY#", 9, 0);
            } else {
                send(client_fd, "OPENING#", 9, 0);
                shared->status = 'o';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);
                shared->status = 'O';
                send(client_fd, "OPENED#", 8, 0);
            }
        } 
        
        else if(strcmp(buffer, "CLOSE#") == 0) {
            if(shared->status == 'C') {
                send(client_fd, "ALREADY#", 9, 0);
            } else {
                send(client_fd, "CLOSING#", 9, 0);
                shared->status = 'c';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);
                shared->status = 'C';
                send(client_fd, "CLOSED#", 8, 0);
            }
        } 
        
        else if(strcmp(buffer, "OPEN_EMERG#") == 0) {
            if(shared->status == 'O') {
                // Door is already open. No action is needed but it's now in emergency mode.
                send(client_fd, "EMERGENCY_MODE#", 15, 0);
            } else {
                // Initiate the emergency open procedure
                send(client_fd, "EMERGENCY_OPENING#", 19, 0);
                shared->status = 'o';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);
                shared->status = 'O';
                send(client_fd, "EMERGENCY_OPENED#", 18, 0);
            }
        } 
        
        else if(strcmp(buffer, "CLOSE_SECURE#") == 0) {
            if(shared->status == 'C') {
                // Door is already closed. No action is needed but it's now in secure mode.
                send(client_fd, "SECURE_MODE#", 13, 0);
            } else {
                // Initiate the secure close procedure
                send(client_fd, "SECURE_CLOSING#", 16, 0);
                shared->status = 'c';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);
                shared->status = 'C';
                send(client_fd, "SECURED#", 9, 0);
            }
        }

        // Unlocking the mutex after processing the command
        pthread_mutex_unlock(&shared->mutex);
        
        // Closing the client connection
        close(client_fd);
    }

    // Cleanup
    munmap(shared, sizeof(shm_door));
    close(shm_fd);
    close(sockfd);

    return 0;
}