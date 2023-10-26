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
    if (argc != 8) {
        fprintf(stderr, "Usage: firealarm {address:port} {temperature threshold} {min detections} {detection period (in microseconds)} {shared memory path} {shared memory offset} {overseer address:port}\n");
        return 1;
    }

    // Initialization of variables from arguments
    char *addr_port = argv[1];
    int temp_threshold = atoi(argv[2]);
    int min_detections = atoi(argv[3]);
    int detection_period = atoi(argv[4]);
    char *shm_path = argv[5];
    int shm_offset = atoi(argv[6]);
    char *overseer_addr_port = argv[7];

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

    shm_alarm *shared = (shm_alarm *)(shm + shm_offset);  /* Pointer to the shared structure */
    shared->alarm = '-';                               /* Initially, the door is considered closed */

    // UDP socket setup for the fire alarm system's communication
    int udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sockfd < 0) {
        perror("Cannot create socket");
        return EXIT_FAILURE;
    }

    // Create a separate sockaddr_in structure for UDP.
    struct sockaddr_in udp_servaddr;
    memset(&udp_servaddr, 0, sizeof(udp_servaddr));
    udp_servaddr.sin_family = AF_INET;

    // If the UDP server is supposed to bind to the same address and port, reset the tokenizer or use different variables.
    char *udp_token; // No need for re-declaration, just use it.
    udp_token = strtok(NULL, ":"); // Resetting to the first token.
    udp_servaddr.sin_addr.s_addr = inet_addr(udp_token);
    udp_token = strtok(NULL, ":");
    udp_servaddr.sin_port = htons(atoi(udp_token));

    // Bind to the UDP socket.
    if (bind(udp_sockfd, (struct sockaddr*)&udp_servaddr, sizeof(udp_servaddr)) < 0) {
        perror("UDP bind failed");
        close(udp_sockfd);
        return EXIT_FAILURE;
    }

    // Setup TCP connection with overseer and send initialization message
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

    char init_msg[100];
    snprintf(init_msg, sizeof(init_msg), "FIREALARM %s:%d HELLO#\n", inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port));
    send(overseer_sock, init_msg, strlen(init_msg), 0);


    return 0;  // Successful exit
}