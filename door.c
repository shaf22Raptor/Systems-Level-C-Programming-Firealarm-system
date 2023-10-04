#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

typedef struct {
    char status;
    pthread_mutex_t mutex;
    pthread_cond_t cond_start;
    pthread_cond_t cond_end;
} DoorStatus;

DoorStatus *doorStatus;

int main(int argc, char *argv[]) {
    if (argc != 7) {
        printf("Usage: %s <id> <address:port> <FAIL_SAFE | FAIL_SECURE> <shared memory path> <shared memory offset> <overseer address:port>\n", argv[0]);
        exit(1);
    }

    // Parse command-line arguments
    char *id = argv[1];
    char *address = strtok(argv[2], ":");
    int port = atoi(strtok(NULL, ":"));
    char *mode = argv[3];
    char *shm_path = argv[4];
    int shm_offset = atoi(argv[5]);
    char *overseer_address = strtok(argv[6], ":");
    int overseer_port = atoi(strtok(NULL, ":"));

    // Open shared memory
    int shm_fd = shm_open(shm_path, O_RDWR | O_CREAT, 0666);
    ftruncate(shm_fd, sizeof(DoorStatus) + shm_offset);
    doorStatus = mmap(0, sizeof(DoorStatus), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, shm_offset);
    doorStatus->status = 'C';

    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, address, &server_addr.sin_addr);
    bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(sockfd, 10);

    // Send initialization message to overseer
    int overseer_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in overseer_addr;
    overseer_addr.sin_family = AF_INET;
    overseer_addr.sin_port = htons(overseer_port);
    inet_pton(AF_INET, overseer_address, &overseer_addr.sin_addr);
    connect(overseer_sockfd, (struct sockaddr *)&overseer_addr, sizeof(overseer_addr));

    char init_msg[BUFFER_SIZE];
    sprintf(init_msg, "DOOR %s %s:%d %s#", id, address, port, mode);
    send(overseer_sockfd, init_msg, strlen(init_msg), 0);
    close(overseer_sockfd);

    // Main loop
    while (1) {
        int client_sockfd = accept(sockfd, NULL, NULL);
        char buffer[BUFFER_SIZE];
        recv(client_sockfd, buffer, BUFFER_SIZE, 0);

        pthread_mutex_lock(&doorStatus->mutex);
        char current_status = doorStatus->status;

        if (strcmp(buffer, "OPEN#") == 0 && current_status == 'O') {
            send(client_sockfd, "ALREADY#", 9, 0);
        } else if (strcmp(buffer, "CLOSE#") == 0 && current_status == 'C') {
            send(client_sockfd, "ALREADY#", 9, 0);
        } else if (strcmp(buffer, "OPEN#") == 0 && current_status == 'C') {
            send(client_sockfd, "OPENING#", 9, 0);
            doorStatus->status = 'o';
            pthread_cond_signal(&doorStatus->cond_start);
            pthread_cond_wait(&doorStatus->cond_end, &doorStatus->mutex);
            doorStatus->status = 'O';
            send(client_sockfd, "OPENED#", 8, 0);
        } else if (strcmp(buffer, "CLOSE#") == 0 && current_status == 'O') {
            send(client_sockfd, "CLOSING#", 9, 0);
            doorStatus->status = 'c';
            pthread_cond_signal(&doorStatus->cond_start);
            pthread_cond_wait(&doorStatus->cond_end, &doorStatus->mutex);
            doorStatus->status = 'C';
            send(client_sockfd, "CLOSED#", 8, 0);
        }
        // Handle other cases like OPEN_EMERG# and CLOSE_SECURE# similarly...

        pthread_mutex_unlock(&doorStatus->mutex);
        close(client_sockfd);
    }

    return 0;
}