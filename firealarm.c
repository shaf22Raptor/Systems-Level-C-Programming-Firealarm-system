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

void send_open_emergency_to_door(struct in_addr door_ip, in_port_t door_port);
void send_door_confirmation(struct in_addr door_ip, in_port_t door_port);
void handle_fire_alarm(void);

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
    struct sockaddr_in myaddr; // server address
    printf("Variables initialized from arguments\n"); // debug message

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
    
    // Extracting UDP address and port from the command line
    char *udp_addr_port = argv[1]; // Assuming the address:port for UDP is the first argument
    struct sockaddr_in udp_servaddr; // server address for UDP
    memset(&udp_servaddr, 0, sizeof(udp_servaddr));
    udp_servaddr.sin_family = AF_INET;
    char *udp_ip = strtok(udp_addr_port, ":");
    char *udp_port = strtok(NULL, ":");
    udp_servaddr.sin_port = htons(udp_port);  // Port number from the command line

    // UDP socket creation
    int udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);  // SOCK_DGRAM for UDP
    if (udp_sockfd < 0) {
        perror("Cannot create UDP socket");
        return EXIT_FAILURE;
    }

    printf("UDP Socket created \n"); // debug message

    // Binding the UDP socket to the local address and port
    if (bind(udp_sockfd, (struct sockaddr*)&udp_servaddr, sizeof(udp_servaddr)) < 0) {
        perror("bind failed for UDP socket");
        exit(EXIT_FAILURE);
    }

    printf("UDP Socket bound to address and listening\n"); // debug message

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

    printf("Connection to overseer done\n"); // debug message

    // Prepare the initialization message
    char init_message[BUFFER_SIZE];  // Buffer for the initialization message.
    snprintf(init_message, sizeof(init_message), "FIREALARM %s HELLO#", addr_port);  // Construct the message with the local address and port.

    // Send the initialization message to the overseer
    ssize_t bytes_sent = send(overseer_sock, init_message, strlen(init_message), 0);
    if (bytes_sent < 0) {
        perror("Failed to send initialization message to overseer");
        close(overseer_sock);  // Close the overseer socket
        exit(EXIT_FAILURE);
    }

    printf("Initialization message sent to overseer\n"); // debug message

    char buffer[BUFFER_SIZE];  // Buffer for incoming data.
    struct sockaddr_in remote_addr; // Address structure for the remote sender.
    socklen_t addr_len = sizeof(remote_addr); // Variable for address length.

    // Main loop
    while (1) {
 
    }

    close(sockfd); // TCP socket for door controller's server
    close(udp_sockfd); // UDP socket for fire alarm system
    close(overseer_sock) // TCP socket for communication with the overseer
    return 0;  // Successful exit
}

// Additional function 1: Send OPEN_EMERG# to a door via TCP
void send_open_emergency_to_door(struct in_addr door_ip, in_port_t door_port) {
    // Create a new TCP socket
    // Connect to the door using the provided IP and port
    // Send the "OPEN_EMERG#" message
    // Close the socket
    // You should handle any errors that might occur during this process
}

// Additional function 2: Send a door confirmation UDP datagram as a reply
void send_door_confirmation(struct in_addr door_ip, in_port_t door_port) {
    // Create a new UDP socket
    // Set up the destination address structure using door_ip and door_port
    // Send the confirmation message to the door system
    // Close the socket
    // Handle any errors appropriately
}

// Additional function 3: Set the alarm in your shared memory and follow the protocol
void handle_fire_alarm(void) {
    // Set the alarm in shared memory
    pthread_mutex_lock(&shared->mutex);
    shared->alarm = 'F'; // Indicate that the fire alarm is active
    pthread_mutex_unlock(&shared->mutex);

    // Call Additional function 1 for every door in your list
    for (int i = 0; i < door_count; ++i) {
        send_open_emergency_to_door(door_addresses[i], door_ports[i]);
    }

    // Start another infinite loop that only processes "DOOR" UDP datagrams
    while (1) {
        // Receive UDP datagrams
        // If you receive "DOOR", call Additional function 1 and Additional function 2
    }
}