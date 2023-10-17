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
#include <fcntl.h>

#define BUFFER_SIZE 256
#define MAX_DOORS 100
#define MAX_DETECTIONS 50

// Shared memory structure
typedef struct {
    char alarm; 
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shm_alarm;

// Door registration datagram structure
typedef struct {
    char header[4]; // {'D', 'O', 'O', 'R'}
    struct in_addr door_addr;
    in_port_t door_port;
} door_datagram;

// Door confirmation datagram structure
typedef struct {
    char header[4]; // {'D', 'R', 'E', 'G'}
    struct in_addr door_addr;
    in_port_t door_port;
} door_confirmation;

// Door list
struct in_addr door_addresses[MAX_DOORS];
in_port_t door_ports[MAX_DOORS];
int door_count = 0;

// Detection timestamps list
long long detection_timestamps[MAX_DETECTIONS];
int detection_count = 0;

// Main function
int main(int argc, char **argv) {
    if (argc != 9) {
        fprintf(stderr, "Usage: ./firealarm {address:port} {temperature threshold} {min detections} {detection period (in microseconds)} {reserved argument} {shared memory path} {shared memory offset} {overseer address:port}\n");
        return 1;
    }

    // Parsing command line arguments and initialization
    const char *bind_address = argv[1];
    int temp_threshold = atoi(argv[2]);
    int min_detections = atoi(argv[3]);
    int detection_period = atoi(argv[4]);
    const char *shm_path = argv[6];
    off_t shm_offset = (off_t)atoi(argv[7]);
    const char *overseer_addr = argv[8];

    // Setup UDP socket
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == -1) {
        perror("socket()");
        return 1;
    }

    // Define server address and port
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(atoi(strchr(bind_address, ':') + 1));
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if(bind(udp_fd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("bind()");
        return 1;
    }

    // Setup shared memory
    int shm_fd = shm_open(shm_path, O_RDWR, 0);
    if(shm_fd == -1) {
        perror("shm_open()");
        return 1;
    }

    shm_alarm *shared = (shm_alarm *)mmap(NULL, sizeof(shm_alarm), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, shm_offset);
    if(shared == MAP_FAILED) {
        perror("mmap()");
        return 1;
    }

    // Parse overseer address and port
    char *overseer_ip = strtok(overseer_addr, ":");
    char *overseer_port_str = strtok(NULL, ":");
    if (!overseer_ip || !overseer_port_str) {
        fprintf(stderr, "Error: Overseer address should be in the format ip:port\n");
        return 1;
    }

    int overseer_port = atoi(overseer_port_str);
    if (overseer_port == 0) {
        fprintf(stderr, "Error: Invalid port number\n");
        return 1;
    }

    struct sockaddr_in overseer_address;
    memset(&overseer_address, 0, sizeof(overseer_address));
    overseer_address.sin_family = AF_INET;

    if (inet_pton(AF_INET, overseer_ip, &overseer_address.sin_addr) <= 0) {
        fprintf(stderr, "Invalid overseer IP address\n");
        return 1;
    }
    overseer_address.sin_port = htons(overseer_port);

    // Create a socket for TCP connection to overseer
    int overseer_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (overseer_sockfd < 0) {
        perror("Cannot create socket");
        return 1;
    }

    // Connect to overseer
    if (connect(overseer_sockfd, (struct sockaddr*)&overseer_address, sizeof(overseer_address)) < 0) {
        perror("Connection to overseer failed");
        close(overseer_sockfd);
        return 1;
    }

    printf("Connected to overseer\n");
    // Main loop
    while (1) {
        char buffer[BUFFER_SIZE];
        struct sockaddr_in src_addr;
        socklen_t addr_len = sizeof(src_addr);
        int received_bytes = recvfrom(udp_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&src_addr, &addr_len);
        
        if (received_bytes <= 0) continue;

        // Handle different datagram types
        if (strncmp(buffer, "FIRE", 4) == 0) {
            // Handle fire emergency
            pthread_mutex_lock(&shared->mutex);
            shared->alarm = 'A'; 
            pthread_mutex_unlock(&shared->mutex);
            pthread_cond_signal(&shared->cond);

            // Open doors for emergency
            for (int i = 0; i < door_count; i++) {
                int door_tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
                if (door_tcp_fd == -1) {
                    perror("socket()");
                    continue;
                }

                struct sockaddr_in doorAddr;
                doorAddr.sin_family = AF_INET;
                doorAddr.sin_port = door_ports[i];
                doorAddr.sin_addr = door_addresses[i];

                if (connect(door_tcp_fd, (struct sockaddr*)&doorAddr, sizeof(doorAddr)) != -1) {
                    char emergency_msg[] = "OPEN_EMERG#";
                    send(door_tcp_fd, emergency_msg, strlen(emergency_msg), 0);
                    close(door_tcp_fd);
                }
            }
        } else if (strncmp(buffer, "TEMP", 4) == 0) {
            // Handle temperature update

            
        } else if (strncmp(buffer, "DOOR", 4) == 0) {
            if (door_count < MAX_DOORS) {
                door_datagram *door_data = (door_datagram *)buffer;
                door_addresses[door_count] = door_data->door_addr;
                door_ports[door_count] = door_data->door_port;
                door_count++;

                // Send a confirmation to the overseer
                door_confirmation confirm;
                memcpy(confirm.header, "DREG", 4);
                confirm.door_addr = door_data->door_addr;
                confirm.door_port = door_data->door_port;

                struct sockaddr_in overseerAddress;
                overseerAddress.sin_family = AF_INET;
                overseerAddress.sin_port = htons(atoi(strchr(overseer_addr, ':') + 1));
                overseerAddress.sin_addr.s_addr = inet_addr(strtok(overseer_addr, ":"));
                sendto(udp_fd, &confirm, sizeof(confirm), 0, (struct sockaddr*)&overseerAddress, sizeof(overseerAddress));
            }
        } else {
            // Ignore other datagrams
            continue;
        }
    }

    // Cleanup
    close(udp_fd);
    return 0;
}