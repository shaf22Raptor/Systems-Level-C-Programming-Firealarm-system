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

// Shared memory structure
typedef struct {
    char status; // 'O', 'C', 'o', 'c'
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

    // Initialization
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

    // Fire alarm connection setup (based on requirement but actual usage not provided in the scenario)
    int firealarm_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in firealarm_addr;
    // Configuration for fire alarm address and port should be provided
    // For this example, we'll use "127.0.0.1:4500"
    firealarm_addr.sin_family = AF_INET;
    firealarm_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    firealarm_addr.sin_port = htons(5000);

    if (connect(firealarm_sock, (struct sockaddr*)&firealarm_addr, sizeof(firealarm_addr)) < 0) {
        perror("Connection to fire alarm failed");
        exit(1);
    }

    // Shared memory initialization
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

    // Connect to overseer and send initialization message
    struct sockaddr_in overseer;
    overseer.sin_family = AF_INET;
    token = strtok(overseer_addr_port, ":");
    overseer.sin_addr.s_addr = inet_addr(token);
    token = strtok(NULL, ":");
    overseer.sin_port = htons(atoi(token));
    
    int o_sock = socket(AF_INET, SOCK_STREAM, 0);
    connect(o_sock, (struct sockaddr*)&overseer, sizeof(overseer));

    char init_msg[100];
    snprintf(init_msg, sizeof(init_msg), "DOOR %d %s %s#\n", id, addr_port, config);
    send_msg(o_sock, init_msg);
    close(o_sock);

    // Normal operation loop
    while (1) {
        int client = accept(sockfd, NULL, NULL);
        char buffer[100];
        int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            close(client);
            continue;
        }
        buffer[bytes] = '\0';

        pthread_mutex_lock(&shared->mutex);
        if (strcmp(buffer, "FIRE_ALARM#") == 0) {
            send_msg(firealarm_sock, "FIRE_ALARM#\n");
        }
        
        if (strcmp(buffer, "OPEN#") == 0) {
            // Implement the logic for opening the door based on the received message
            if (shared->status == 'O') {
                send_msg(client, "ALREADY#\n");
            } else {
                shared->status = 'o';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);
                shared->status = 'O';
                send_msg(client, "OPENED#\n");
            }
        } else if (strcmp(buffer, "CLOSE#") == 0) {
            // Implement the logic for closing the door based on the received message
            if (shared->status == 'C') {
                send_msg(client, "ALREADY#\n");
            } else {
                shared->status = 'c';
                pthread_cond_signal(&shared->cond_start);
                pthread_cond_wait(&shared->cond_end, &shared->mutex);
                shared->status = 'C';
                send_msg(client, "CLOSED#\n");
            }
        }
        // Add logic for other commands as needed

        pthread_mutex_unlock(&shared->mutex);
        close(client);
    }

    close(firealarm_sock);
    close(shm_fd);
    return 0;
}