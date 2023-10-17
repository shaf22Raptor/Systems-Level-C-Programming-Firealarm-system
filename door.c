/* 
 * This file implements a door controller for safety-critical applications.
 * The controller manages door state and communicates with an overseer program.
 * Safety-critical compliance deviations and justifications (if any) should be documented here.
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

void send_msg(int sockfd, const char* msg) {
    send(sockfd, msg, strlen(msg), 0);
}

int main(int argc, char **argv) {
    if (argc != 7) {
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

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    char *token = strtok(addr_port, ":");
    servaddr.sin_addr.s_addr = inet_addr(token);
    token = strtok(NULL, ":");
    servaddr.sin_port = htons(atoi(token));

    bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    listen(sockfd, 10);

    /* Shared memory initialization */
    int shm_fd = shm_open(shm_path, O_RDWR, 0);
    if (shm_fd == -1) {
        perror("shm_open()");
        exit(1);
    }

    struct stat shm_stat;
    if (fstat(shm_fd, &shm_stat) == -1) {
        perror("fstat()");
        exit(1);
    }

    char *shm = mmap(NULL, shm_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap()");
        exit(1);
    }

    shm_door *shared = (shm_door *)(shm + shm_offset);
    shared->status = 'C';

    /* Connect to overseer and send initialization message */
    struct sockaddr_in overseer_addr;
    memset(&overseer_addr, 0, sizeof(overseer_addr));
    overseer_addr.sin_family = AF_INET;

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

    int overseer_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (overseer_sock < 0) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    if (connect(overseer_sock, (struct sockaddr*)&overseer_addr, sizeof(overseer_addr)) < 0) {
        perror("Connection to overseer failed");
        printf("Failed to connect to overseer at IP: %s, Port: %d\n", overseer_ip, overseer_port); // Log IP and port
        close(overseer_sock);
        exit(EXIT_FAILURE);
    } else {
        printf("Connected to overseer at IP: %s, Port: %d\n", overseer_ip, overseer_port); // Success case
    }


    /* Send an initialization message to the overseer */
    char init_msg[100];
    snprintf(init_msg, sizeof(init_msg), "DOOR %d %s:%d %s#\n", id, inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port), config);
    ssize_t send_res = send(overseer_sock, init_msg, strlen(init_msg), 0);
    if (send_res < 0) {
        perror("Failed to send initialization message");
        printf("Error occurred while sending message to overseer. Message: %s\n", init_msg); // Log the message content
        close(overseer_sock);
        exit(EXIT_FAILURE);
    } else {
        printf("Initialization message sent to overseer: %s\n", init_msg); // Log the sent message
    }

    /* Normal operation loop */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (client < 0) {
            perror("accept failed");
            printf("Error occurred while accepting connection. sockfd: %d\n", sockfd); // Log the socket descriptor
            continue; /* Skip further processing for this client */
        }

        printf("Client connected\n"); // Added debug message

        char buffer[100];
        int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
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

        printf("Received message: %s\n", buffer); // Added debug message

        /* Lock the mutex before checking or changing the status */
        pthread_mutex_lock(&shared->mutex);


        if (strcmp(buffer, "OPEN#") == 0) {
            /* Implement the logic for opening the door based on the received message */
            if (shared->status == 'O') {
                printf("Door is already open\n"); // Debug message
                send_msg(client, "ALREADY#\n");
            } else {
                printf("Opening door...\n"); // Debug message
                /* Inform the overseer that the door is opening */
                send_msg(overseer_sock, "OPENING#\n");

                /* Start the door opening process */
                shared->status = 'o';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);

                /* After the door has opened, update the status and notify the client and overseer */
                shared->status = 'O';
                send_msg(client, "OPENED#\n");
            }
        } else if (strcmp(buffer, "CLOSE#") == 0) {
            /* Implement the logic for closing the door based on the received message */
            if (shared->status == 'C') {
                printf("Door is already closed\n"); // Debug message
                send_msg(client, "ALREADY#\n");
            } else {
                printf("Closing door...\n"); // Debug message
                /* Inform the overseer that the door is closing */
                send_msg(overseer_sock, "CLOSING#\n");

                /* Start the door closing process */
                shared->status = 'c';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);

                /* After the door has closed, update the status and notify the client and overseer */
                shared->status = 'C';
                send_msg(client, "CLOSED#\n");
            }
        }
        else if (strcmp(buffer, "OPEN_EMERG#") == 0) {
            if (shared->status == 'C' || shared->status == 'c') {
                printf("Emergency opening...\n"); // Debug message

                /* Inform the overseer that there is an emergency opening */
                send_msg(overseer_sock, "EMERGENCY_OPENING#\n");

                /* Start the door opening process */
                shared->status = 'o';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);

                /* After the door has opened, update the status and notify the client and overseer */
                shared->status = 'O';
                send_msg(client, "EMERGENCY_OPENED#\n");
            } else {
                printf("Door is already open\n"); // Debug message
                send_msg(client, "ALREADY#\n");
            }
        }

        /* Handling secure close command */
        else if (strcmp(buffer, "CLOSE_SECURE#") == 0) {
            if (shared->status == 'O' || shared->status == 'o') {
                printf("Secure closing...\n"); // Debug message

                /* Inform the overseer that there is a secure closing */
                send_msg(overseer_sock, "SECURE_CLOSING#\n");

                /* Start the door closing process */
                shared->status = 'c';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);

                /* After the door has closed, update the status and notify the client and overseer */
                shared->status = 'C';
                send_msg(client, "SECURE_CLOSED#\n");
            } else {
                printf("Door is already closed\n"); // Debug message
                send_msg(client, "ALREADY#\n");
            }
        }
        pthread_mutex_unlock(&shared->mutex);
        close(client);
    }

    close(overseer_sock);
    close(shm_fd);
    return 0;
}