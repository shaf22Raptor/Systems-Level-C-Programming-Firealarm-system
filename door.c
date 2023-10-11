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
#include <arpa/inet.h>
#include <netinet/in.h>

#define BUFFER_SIZE 16

struct door_shm {
    char status; // 'O', 'C', 'o', 'c'
    pthread_mutex_t mutex;
    pthread_cond_t cond_start;
    pthread_cond_t cond_end;
};

void configureServerAddress(struct sockaddr_in *serverAddr, const char *ip, int port) {
    serverAddr->sin_family = AF_INET;
    serverAddr->sin_port = htons(port);
    inet_pton(AF_INET, ip, &serverAddr->sin_addr);
}

int main(int argc, char **argv) {
    if (argc != 7) {
        fprintf(stderr, "Usage: {id} {address:port} {FAIL_SAFE | FAIL_SECURE} {shared memory path} {shared memory offset} {overseer address:port}\n");
        exit(1);
    }

    int id = atoi(argv[1]);
    char *address = strtok(argv[2], ":");
    int port = atoi(strtok(NULL, ":"));
    char *security_mode = argv[3];
    char *shm_path = argv[4];
    off_t shm_offset = (off_t)atoi(argv[5]);
    char *overseer_addr = strtok(argv[6], ":");
    int overseer_port = atoi(strtok(NULL, ":"));

    // TCP connection setup
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serverAddr;
    configureServerAddress(&serverAddr, overseer_addr, overseer_port);

    if (connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("connection failed");
        exit(EXIT_FAILURE);
    }

    char init_message[BUFFER_SIZE];
    snprintf(init_message, sizeof(init_message), "DOOR %d %s:%d %s#", id, address, port, security_mode);
    send(sockfd, init_message, strlen(init_message), 0);

    // Shared memory setup
    int shm_fd = shm_open(shm_path, O_RDWR, 0666);
    struct door_shm *shared = mmap(NULL, sizeof(struct door_shm), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, shm_offset);

    while (1) {
        pthread_mutex_lock(&shared->mutex);

        int client_sock = accept(sockfd, NULL, NULL);
        char buffer[BUFFER_SIZE];
        recv(client_sock, buffer, sizeof(buffer), 0);

        if (strcmp(buffer, "OPEN#") == 0) {
            if (shared->status == 'O') {
                send(client_sock, "ALREADY#", 9, 0);
            } else {
                send(client_sock, "OPENING#", 9, 0);
                shared->status = 'o';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);
                shared->status = 'O';
                send(client_sock, "OPENED#", 8, 0);
            }
        } else if (strcmp(buffer, "CLOSE#") == 0) {
            if (shared->status == 'C') {
                send(client_sock, "ALREADY#", 9, 0);
            } else {
                send(client_sock, "CLOSING#", 9, 0);
                shared->status = 'c';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);
                shared->status = 'C';
                send(client_sock, "CLOSED#", 8, 0);
            }
        } else if (strcmp(buffer, "OPEN_EMERG#") == 0) {
        if (shared->status == 'O') {
            // Door is already open, respond and move to emergency mode
            send(client_sock, "EMERGENCY_MODE#", 15, 0);
        } else {
            // Open the door for emergency and move to emergency mode
            send(client_sock, "OPENING#", 9, 0);
            shared->status = 'o';
            pthread_cond_signal(&shared->cond_start);
            pthread_cond_wait(&shared->cond_end, &shared->mutex);
            shared->status = 'O';
            send(client_sock, "OPENED#", 8, 0);
            send(client_sock, "EMERGENCY_MODE#", 15, 0);
        }

        } else if (strcmp(buffer, "CLOSE_SECURE#") == 0) {
            if (shared->status == 'C') {
                // Door is already closed, respond and move to secure mode
                send(client_sock, "SECURE_MODE#", 12, 0);
            } else {
                // Close the door for security and move to secure mode
                send(client_sock, "CLOSING#", 9, 0);
                shared->status = 'c';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);
                shared->status = 'C';
                send(client_sock, "CLOSED#", 8, 0);
                send(client_sock, "SECURE_MODE#", 12, 0);
            }
        }
        
        pthread_mutex_unlock(&shared->mutex);
        close(client_sock);
    }

    close(sockfd);
    pthread_mutex_destroy(&shared->mutex);
    pthread_cond_destroy(&shared->cond_start);
    pthread_cond_destroy(&shared->cond_end);
    munmap(shared, sizeof(struct door_shm));
    close(shm_fd);

    return 0;
}