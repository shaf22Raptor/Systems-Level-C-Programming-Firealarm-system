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


// Fire emergency datagram
typedef struct {
    char header[4]; // {'F', 'I', 'R', 'E'}
} fire_datagram;

// Temperature sensor controller datagrams
struct addr_entry {
  struct in_addr sensor_addr;
  in_port_t sensor_port;
};

struct datagram_format {
  char header[4]; // {'T', 'E', 'M', 'P'}
  struct timeval timestamp;
  float temperature;
  uint16_t id;
  uint8_t address_count;
  struct addr_entry address_list[50];
};

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
    printf("Program started\n");

    if (argc != 9) {
        fprintf(stderr, "Usage: firealarm {address:port} {temperature threshold} {min detections} {detection period (in microseconds)} {reserved argument} {shared memory path} {shared memory offset} {overseer address:port}\n");
        return 1;
    }

    // Initialization of variables from arguments
    char *addr_port = argv[1];
    int temp_threshold = atoi(argv[2]);
    int min_detections = atoi(argv[3]);
    int detection_period = atoi(argv[4]);
    char *shm_path = argv[6]; 
    int shm_offset = atoi(argv[7]); 
    char *overseer_addr_port = argv[8];
    int udp_sockfd; // Socket descriptor
    struct sockaddr_in myaddr; // server address
    char *udp_ip = strtok(addr_port, ":");
    char *udp_port_str = strtok(NULL, ":");
    int udp_port = atoi(udp_port_str);
    printf("Variables initialized from arguments\n"); // debug message

    // if (!udp_ip || !udp_port_str) {
    //     fprintf(stderr, "Error: address should be in the format ip:port\n");
    //     exit(EXIT_FAILURE);
    // }
    // if (udp_port == 0) {
    //     fprintf(stderr, "Error: Invalid port number\n");
    //     exit(EXIT_FAILURE);
    // }

    /* Socket setup for the door controller's server */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);   /* Create a socket for communication */
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    char *token = strtok(addr_port, ":");
    servaddr.sin_addr.s_addr = inet_addr(token);
    token = strtok(NULL, ":");
    servaddr.sin_port = htons(atoi(token));         /* Assign port to this socket */
    printf("Socket created \n"); // debug message

    /* Binding the socket to the server address */
    if (bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    listen(sockfd, 10);
    printf("Socket bound to address and listening\n"); // debug message

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

    shm_alarm *shared = (shm_alarm *)(shm + shm_offset);  /* Pointer to the shared structure */
    shared->alarm = '-';                               /* Initially, the door is considered closed */
    printf("Shared memory initialized\n"); // debug message
    
    /* Setup for the UDP socket */
    struct sockaddr_in udp_servaddr;
    memset(&udp_servaddr, 0, sizeof(udp_servaddr));
    udp_servaddr.sin_family = AF_INET;
    udp_servaddr.sin_addr.s_addr = inet_addr(udp_ip);  // IP address from the command line
    udp_servaddr.sin_port = htons(udp_port);  // Port number from the command line

    // UDP socket creation for fire alarm system's communication
    udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sockfd < 0) {
        perror("Cannot create UDP socket");
        return EXIT_FAILURE;
    }

    // Binding the UDP socket to the local address and port
    if (bind(udp_sockfd, (struct sockaddr*)&udp_servaddr, sizeof(udp_servaddr)) < 0) {
        perror("bind failed for UDP socket");
        exit(EXIT_FAILURE);
    }

    printf("UDP socket created and bound to address\n"); // Debug message

    char buffer[BUFFER_SIZE];  // Buffer for incoming data.
    struct sockaddr_in remote_addr; // Address structure for the remote sender.
    socklen_t addr_len = sizeof(remote_addr); // Variable for address length.

    // Main loop
    while (1) {
 
    }

    close(sockfd); // TCP socket for door controller's server
    close(udp_sockfd); // UDP socket for fire alarm system
    // close(overseer_port); // TCP socket for communication with the overseer
    return 0;  // Successful exit
}