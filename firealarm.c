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
        } else if (strncmp(buffer, "TEMP", 4) == 0) {
            // Handle temperature update
        } else if (strncmp(buffer, "DOOR", 4) == 0) {
            door_datagram *door_data = (door_datagram *)buffer;
            // Register the door
        } else {
            // Ignore other datagrams
            continue;
        }
    }

    // Cleanup
    close(udp_fd);
    return 0;
}