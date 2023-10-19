/* 
 * This is the main executable file for a door controller in safety-critical applications.
 * It maintains the door state and ensures communication with an overseer program.
 * The controller can handle commands to open or close the door and responds with the door's current state.
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* Shared memory structure */
typedef struct {
    char status; /* 'O', 'C', 'o', 'c' */
    pthread_mutex_t mutex;
    pthread_cond_t cond_start;
    pthread_cond_t cond_end;
} shm_door;

/* Function to send a message over a socket */
void send_msg(int sockfd, const char* msg) {
    send(sockfd, msg, strlen(msg), 0);
}

int main(int argc, char **argv) {
    if (argc != 7) {
        /* Incorrect number of arguments */
        fprintf(stderr, "Usage: door {id} {address:port} {FAIL_SAFE | FAIL_SECURE} {shared memory path} {shared memory offset} {overseer address:port}\n");
        exit(1);
    }

    /* Initialization */
    int id = atoi(argv[1]);
    char *addr_port = argv[2];
    char *config = argv[3];
    const char *shm_path = argv[4];
    int shm_offset = atoi(argv[5]);
    char *overseer_addr_port = argv[6];

    /* Socket setup for the door controller's server */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);   /* Create a socket for communication */
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    char *token = strtok(addr_port, ":");
    servaddr.sin_addr.s_addr = inet_addr(token);
    token = strtok(NULL, ":");
    servaddr.sin_port = htons(atoi(token));         /* Assign port to this socket */

    /* Binding the socket to the server address */
    if (bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    listen(sockfd, 10);

    /* Shared memory initialization */
    int shm_fd = shm_open(shm_path, O_RDWR, 0);
    if (shm_fd == -1) {
        perror("shm_open()");
        exit(1);
    }

    struct stat shm_stat; /* To obtain file size */
    if (fstat(shm_fd, &shm_stat) == -1) {
        perror("fstat()");
        exit(1);
    }

    /* Map the shared memory in the address space of the process */
    char *shm = mmap(NULL, shm_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap()");
        exit(1);
    }

    shm_door *shared = (shm_door *)(shm + shm_offset);  /* Pointer to the shared structure */
    shared->status = 'C';                               /* Initially, the door is considered closed */

    /* Connect to overseer and send initialization message */
    struct sockaddr_in overseer_addr;
    memset(&overseer_addr, 0, sizeof(overseer_addr));
    overseer_addr.sin_family = AF_INET;

    /* Parsing overseer IP address and port */
    char *overseer_ip = strtok(overseer_addr_port, ":");
    char *overseer_port_str = strtok(NULL, ":");
    if (!overseer_ip || !overseer_port_str) {
        fprintf(stderr, "Error: Overseer address should be in the format ip:port\n");
        exit(EXIT_FAILURE);
    }

    int overseer_port = atoi(overseer_port_str);
    if (overseer_port == 0) {
        fprintf(stderr, "Error: Invalid port number\n");
        exit(EXIT_FAILURE);
    }

    if (inet_pton(AF_INET, overseer_ip, &overseer_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid overseer IP address\n");
        exit(EXIT_FAILURE);
    }
    overseer_addr.sin_port = htons(overseer_port);

    /* Communication setup with the overseer */
    int overseer_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (overseer_sock < 0) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    if (connect(overseer_sock, (struct sockaddr*)&overseer_addr, sizeof(overseer_addr)) < 0) {
        perror("Connection to overseer failed");
        close(overseer_sock);
        exit(EXIT_FAILURE);
    }

    /* Send an initialization message to the overseer */
    char init_msg[100];
    snprintf(init_msg, sizeof(init_msg), "DOOR %d %s:%d %s#\n", id, inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port), config);
    ssize_t send_res = send(overseer_sock, init_msg, strlen(init_msg), 0);
    if (send_res < 0) {
        perror("Failed to send initialization message");
        close(overseer_sock);
        exit(EXIT_FAILURE);
    } 

    /* Main operational loop starts */
    while (1) {
        /* Accepting a client connection */
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (client < 0) {
            perror("accept failed");
            printf("Error occurred while accepting connection. sockfd: %d\n", sockfd);  
            continue;                                                                   
        }

    char buffer[100];                                               /* Buffer to store client messages */
        int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);    /* Receive data from the client socket */
        if (bytes <= 0) {
            /* Handle errors or connection closure */
            if (bytes == 0) {
                /* Connection closed */
                printf("Client disconnected\n");
            } else {
                perror("recv failed");
            }
            close(client);
            continue;
        }
        buffer[bytes] = '\0'; /* Null-terminate the string */

        /* Lock the mutex before accessing the shared status */
        pthread_mutex_lock(&shared->mutex);
        char current_status = shared->status; 
        pthread_mutex_unlock(&shared->mutex);

        /* Processing client commands and preparing a response */
        char response[100]; // Buffer to hold responses to send back.
        if (strncmp(buffer, "STATE#", 6) == 0) {
            /* Query door state */
            pthread_mutex_lock(&shared->mutex);                 
            snprintf(response, sizeof(response), "STATE %c#\n", shared->status);
            pthread_mutex_unlock(&shared->mutex);               
        } else if (strncmp(buffer, "OPEN#", 5) == 0) {
            /* Open door */
            pthread_mutex_lock(&shared->mutex);
            shared->status = 'O';
            strcpy(response, "OPENING#\n");
            pthread_mutex_unlock(&shared->mutex);
        } else if (strncmp(buffer, "CLOSE#", 6) == 0) {
            /* Close door */
            pthread_mutex_lock(&shared->mutex);
            shared->status = 'C';
            strcpy(response, "CLOSING#\n");
            pthread_mutex_unlock(&shared->mutex);
        } else if (strncmp(buffer, "OPEN_EMERG#", 11) == 0) {
            /* Emergency command to forcefully open the door */
            if (shared->status == 'C' || shared->status == 'c') {  
                pthread_mutex_lock(&shared->mutex);

                shared->status = 'o';  
                pthread_cond_signal(&shared->cond_start);  

                while (shared->status != 'O') {
                    pthread_cond_wait(&shared->cond_end, &shared->mutex);  
                }

                pthread_mutex_unlock(&shared->mutex);
                strcpy(response, "EMERGENCY_MODE#\n");
            } else if (shared->status == 'O') {
                strcpy(response, "EMERGENCY_MODE#\n");
            }
        } else if (strncmp(buffer, "CLOSE_SECURE#", 13) == 0) {
            /* Command to close the door securely in response to a security protocol */
            if (shared->status == 'O' || shared->status == 'o') {  
                pthread_mutex_lock(&shared->mutex);

                shared->status = 'c';  
                pthread_cond_signal(&shared->cond_start);  

                while (shared->status != 'C') {
                    pthread_cond_wait(&shared->cond_end, &shared->mutex); 
                }

                pthread_mutex_unlock(&shared->mutex);
                strcpy(response, "SECURE_MODE#\n");
            } else if (shared->status == 'C') {
                strcpy(response, "SECURE_MODE#\n");
            }   
        } else {
            /* Handle unrecognized commands */
            fprintf(stderr, "Invalid command: %s\n", buffer);
            strcpy(response, "ERROR Invalid command#\n"); 
        }

        send_msg(client, response);
        /*Close the client connection after handling the request*/
        close(client);
    }
    
    /* Clean up resources */
    munmap(shm, shm_stat.st_size);
    close(overseer_sock);
    close(shm_fd);
    return 0;
}