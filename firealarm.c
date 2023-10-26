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
        fprintf(stderr, "Usage: firealarm {address:port} {temperature threshold} {min detections} {detection period (in microseconds)} {shared memory path} {shared memory offset} {overseer address:port}\n");
        return 1;
    }

    // Parsing command line arguments and initialization
    const char *bind_address = argv[1];
    int temp_threshold = atoi(argv[2]);
    int min_detections = atoi(argv[3]);
    int detection_period = atoi(argv[4]);
    const char *shm_path = argv[6];
    off_t shm_offset = (off_t)atoi(argv[7]);
    const char *overseer_addr = argv[7];
    char *server_ip = strtok(bind_address, ":");
    char *server_port_str = strtok(NULL, ":");
    int server_port = atoi(server_port_str);

    // Setup UDP socket
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(server_port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    

    if (bind(udp_fd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Error binding socket");
        close(udp_fd);  // Always close the file descriptor if not proceeding further
        exit(EXIT_FAILURE);
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
    char *temp_overseer_addr = strdup(overseer_addr);  // Remember to free it later
    char *overseer_ip = strtok(temp_overseer_addr, ":");
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
    overseer_address.sin_port = htons(overseer_port);
    if (inet_pton(AF_INET, overseer_ip, &overseer_address.sin_addr) <= 0) {
        fprintf(stderr, "Invalid overseer IP address\n");
        free(temp_overseer_addr);
        return 1;
    }
    
    free(temp_overseer_addr);  // Free the duplicated address


    // Create a socket for TCP connection to overseer
    int overseer_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (overseer_sockfd < 0) {
        perror("Cannot create socket");
        return 1;
    }

    // Connect to overseer
    if (connect(overseer_sockfd, (struct sockaddr *)&overseer_address, sizeof(overseer_address)) < 0) {
    perror("connect()");
    return 1;
    }

    // Notify overseer about the fire alarm service
    char init_msg[BUFFER_SIZE];
    snprintf(init_msg, BUFFER_SIZE, "FIREALARM %s:%d#", inet_ntoa(serverAddr.sin_addr), ntohs(serverAddr.sin_port));
    if (send(overseer_sockfd, init_msg, strlen(init_msg), 0) < 0) {
        perror("send()");
        return 1;
    }

    // Main loop: listen for temperature data or door registration
    while (1) {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        char buffer[BUFFER_SIZE];

        // Receive datagrams
    int recvLen = recvfrom(udp_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&clientAddr, &addrLen);
    if (recvLen > 0) {
        // Handle FIRE datagram
        if (memcmp(buffer, "FIRE", 4) == 0) {
            pthread_mutex_lock(&shared->mutex);
            shared->alarm = 'A';
            pthread_mutex_unlock(&shared->mutex);
            pthread_cond_signal(&shared->cond);

            // Send OPEN_EMERG# to all registered doors
            char message[] = "OPEN_EMERG#";
            for (int i = 0; i < door_count; ++i) {
                struct sockaddr_in doorAddr;
                memset(&doorAddr, 0, sizeof(doorAddr));
                doorAddr.sin_family = AF_INET;
                doorAddr.sin_port = door_ports[i];
                doorAddr.sin_addr = door_addresses[i];

                int door_sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (door_sockfd < 0) continue;  // If unable to create a socket, skip to the next door

                if (connect(door_sockfd, (struct sockaddr *)&doorAddr, sizeof(doorAddr)) >= 0) {
                    send(door_sockfd, message, sizeof(message), 0);
                    close(door_sockfd);
                }
            }
        }
        // Handle TEMP datagram
        else if (memcmp(buffer, "TEMP", 4) == 0) {
           // Parse the datagram
            struct datagram_format *data = (struct datagram_format *)buffer;
            float temperature = data->temperature;
            long long timestamp = data->timestamp.tv_sec; // Assuming it's in seconds for simplicity

            // Check temperature and record detection if it's above the threshold
            if (temperature >= temp_threshold) {
                if (detection_count < MAX_DETECTIONS) {
                    detection_timestamps[detection_count++] = timestamp;
                }

                // Check if we need to trigger the alarm
                int relevant_detections = 0;
                for (int i = 0; i < detection_count; ++i) {
                    if ((timestamp - detection_timestamps[i]) < detection_period) {
                        ++relevant_detections;
                    }
                }

                if (relevant_detections >= min_detections) {
                    // Trigger alarm
                    pthread_mutex_lock(&shared->mutex);
                    shared->alarm = 'A';
                    pthread_cond_signal(&shared->cond);
                    pthread_mutex_unlock(&shared->mutex);

                    // Resetting detection count can be considered based on the logic you need
                    detection_count = 0;
                }
            }
        }
        // Handle DOOR registration
        else if (memcmp(buffer, "DOOR", 4) == 0 && recvLen == sizeof(door_datagram)) {
            door_datagram *datagram = (door_datagram *)buffer;

            if (door_count < MAX_DOORS) {
                // Add the door to your list
                door_addresses[door_count] = datagram->door_addr;
                door_ports[door_count] = datagram->door_port;
                ++door_count;

                // Send back a confirmation
                door_confirmation confirm = {
                    .header = {'D', 'R', 'E', 'G'},
                    .door_addr = datagram->door_addr,
                    .door_port = datagram->door_port
                };

                sendto(udp_fd, &confirm, sizeof(confirm), 0, (struct sockaddr *)&clientAddr, addrLen);
            }
        }
    }

    // Cleanup
    free(server_ip);
    close(overseer_sockfd);
    close(udp_fd);
    munmap(shared, sizeof(*shared));
    close(shm_fd);

    return 0;
}