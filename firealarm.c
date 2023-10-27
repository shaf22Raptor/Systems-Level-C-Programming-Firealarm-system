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

// Fire emergency datagram
char fire_emergency_header[] = {'F', 'I', 'R', 'E', '\0'}; 

//Global shared
shm_alarm *shared;

// Main function
int main(int argc, char **argv) {
    printf("Program started\n");

    if (argc != 9) {
        fprintf(stderr, "Usage: firealarm {address:port} {temperature threshold} {min detections} {detection period (in microseconds)} {reserved argument} {shared memory path} {shared memory offset} {overseer address:port}\n");
        return 1;
    }

    // Initialisation of variables from arguments
    char *addr_port = argv[1];
    int temp_threshold = atoi(argv[2]);
    int min_detections = atoi(argv[3]);
    int detection_period = atoi(argv[4]);
    char *shm_path = argv[6]; 
    int shm_offset = atoi(argv[7]); 
    char *overseer_addr_port = argv[8];
    // struct sockaddr_in myaddr; // server address
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

    /* Shared memory initialisation */
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

    shared = (shm_alarm *)(shm + shm_offset);  /* Pointer to the shared structure */
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

    if (inet_aton(udp_ip, &udp_servaddr.sin_addr) == 0) { // Convert from str to binary form
        fprintf(stderr, "Invalid IP address format for UDP.\n");
        exit(EXIT_FAILURE);
    }

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

    /* Connect to overseer and send initialisation message */
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

    // Prepare the initialisation message
    char init_message[BUFFER_SIZE];  // Buffer for the initialisation message.
    snprintf(init_message, sizeof(init_message), "FIREALARM %s HELLO#", addr_port);  // Construct the message with the local address and port.

    // Send the initialisation message to the overseer
    ssize_t bytes_sent = send(overseer_sock, init_message, strlen(init_message), 0);
    if (bytes_sent < 0) {
        perror("Failed to send initialisation message to overseer");
        close(overseer_sock);  // Close the overseer socket
        exit(EXIT_FAILURE);
    }

    printf("Initialisation message sent to overseer\n"); // debug message

    // char buffer[BUFFER_SIZE];  // Buffer for incoming data.
    // struct sockaddr_in remote_addr; // Address structure for the remote sender.
    // socklen_t addr_len = sizeof(remote_addr); // Variable for address length.

    // // Main loop
    // while (1) {
    //     memset(buffer, 0, BUFFER_SIZE);  // Clear the buffer.
        
    //     // Receive datagrams
    //     ssize_t bytes_received = recvfrom(udp_sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&remote_addr, &addr_len);
    //     if (bytes_received < 0) {
    //         perror("Failed to receive UDP datagram");
    //         continue;  // Skip the current iteration and wait for next datagram.
    //     }

    //     // Handle "FIRE" datagram
    //     if (strncmp(buffer, fire_emergency_header, 4) == 0) {
    //         handle_fire_alarm();
    //         continue;
    //     }
        
    //     // Handle "DOOR" datagram
    //     if (strncmp(buffer, "DOOR", 4) == 0) {
    //         if (door_count < MAX_DOORS) {
    //             door_datagram *door_data = (door_datagram *)buffer;
    //             door_addresses[door_count] = door_data->door_addr;
    //             door_ports[door_count] = door_data->door_port;
    //             door_count++;
    //             send_door_confirmation(door_data->door_addr, door_data->door_port);
    //         } else {
    //             printf("Warning: Maximum number of doors reached. Cannot register more doors.\n");
    //         }
    //         continue;
    //     }
        
    //     // Handle "TEMP" datagram
    //     if (strncmp(buffer, "TEMP", 4) == 0) {
    //         struct datagram_format *temp_data = (struct datagram_format *)buffer;
            
    //         // Check if the temperature exceeds the threshold
    //         if (temp_data->temperature > temp_threshold) {
    //             if (detection_count < MAX_DETECTIONS) {
    //                 detection_timestamps[detection_count] = temp_data->timestamp.tv_sec * 1000000 + temp_data->timestamp.tv_usec;  // Convert to microseconds
    //                 detection_count++;
    //             }
                
    //             // Check the number of detections within the detection period
    //             long long current_time = temp_data->timestamp.tv_sec * 1000000 + temp_data->timestamp.tv_usec;
    //             int detections_within_period = 0;
    //             for (int i = 0; i < detection_count; i++) {
    //                 if (current_time - detection_timestamps[i] <= detection_period) {
    //                     detections_within_period++;
    //                 }
    //             }
                
    //             if (detections_within_period >= min_detections) {
    //                 handle_fire_alarm();
    //             }
    //         }
    //         continue;
    //     }
    // }
    munmap(shm, shm_stat.st_size);
    close(sockfd); // TCP socket for door controller's server
    close(udp_sockfd); // UDP socket for fire alarm system
    close(overseer_sock); // TCP socket for communication with the overseer
    return 0;  // Successful exit
}

// Additional function 1: Send OPEN_EMERG# to a door via TCP
void send_open_emergency_to_door(struct in_addr door_ip, in_port_t door_port) {
    // Create a new TCP socket
    // Connect to the door using the provided IP and port
    // Send the "OPEN_EMERG#" message
    // Close the socket
    // and should handle any errors that might occur during this process

    // Message to be sent to the door in case of an emergency
    char emergency_message[] = "OPEN_EMERG#";
    
    // Create a new socket for TCP connection
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error in socket creation");
        return;
    }

    // Set up the destination address structure
    struct sockaddr_in door_address;
    memset(&door_address, 0, sizeof(door_address)); // Ensuring that the struct is empty
    door_address.sin_family = AF_INET;              // Standard IPv4 address
    door_address.sin_addr = door_ip;                // IP address comes from function argument
    door_address.sin_port = door_port;              // Port number comes from function argument

    // Establish a connection to the door system
    if (connect(sock, (struct sockaddr*)&door_address, sizeof(door_address)) < 0) {
        perror("Connection to the door system failed");
        close(sock);
        return;
    }

    // Send the emergency message to the door system
    ssize_t bytes_sent = send(sock, emergency_message, strlen(emergency_message), 0);
    if (bytes_sent < 0) {
        perror("Failed to send message to door system");
    } else if (bytes_sent < strlen(emergency_message)) {
        printf("Warning: Not all bytes sent to door system\n");
    }

    // Close the socket after sending the data
    close(sock);
}

// Additional function 2: Send a door confirmation UDP datagram as a reply
void send_door_confirmation(struct in_addr door_ip, in_port_t door_port) {
    // Create a new UDP socket
    // Set up the destination address structure using door_ip and door_port
    // Send the confirmation message to the door system
    // Close the socket
    // Handle any errors appropriately

    // Message to be sent as confirmation
    door_confirmation confirmation;
    memcpy(confirmation.header, "DREG", 4); // "DREG" signifies a door confirmation message
    confirmation.door_addr = door_ip;
    confirmation.door_port = door_port;

    // Create a new socket for UDP connection
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Error in socket creation");
        return;
    }

    // Set up the destination address structure
    struct sockaddr_in door_address;
    memset(&door_address, 0, sizeof(door_address)); // Ensuring that the struct is empty
    door_address.sin_family = AF_INET;              // Standard IPv4 address
    door_address.sin_addr = door_ip;                // IP address comes from function argument
    door_address.sin_port = door_port;              // Port number comes from function argument

    // Send the confirmation message to the door system
    ssize_t bytes_sent = sendto(sock, (const void *)&confirmation, sizeof(confirmation), 0, 
                                (const struct sockaddr*)&door_address, sizeof(door_address));
    if (bytes_sent < 0) {
        perror("Failed to send confirmation to door system");
    } else if (bytes_sent < sizeof(confirmation)) {
        printf("Warning: Not all bytes sent to door system\n");
    }

    // Close the socket after sending the data
    close(sock);
}

// Additional function 3: Set the alarm in your shared memory and follow the protocol
void handle_fire_alarm(void) {
    // Set the alarm in shared memory
    pthread_mutex_lock(&shared->mutex);
    shared->alarm = 'F'; // Indicate that the fire alarm is active
    pthread_cond_broadcast(&shared->cond); // Notify any waiting threads about the alarm state
    pthread_mutex_unlock(&shared->mutex);

    // Notify every door about the emergency
    for (int i = 0; i < door_count; ++i) {
        send_open_emergency_to_door(door_addresses[i], door_ports[i]);
    }

    // Prepare to receive "DOOR" UDP datagrams
    int udp_sockfd;
    struct sockaddr_in myaddr;
    int recvlen;
    unsigned char buf[BUFFER_SIZE]; // Buffer for receiving incoming messages.
    
    // Create a UDP socket
    if ((udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("cannot create socket");
        return;
    }

    // Bind the socket to any valid IP address and a specific port
    memset((char *)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on any address
    myaddr.sin_port = htons(0); // Listen on any free port, replace with specific if required

    if (bind(udp_sockfd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
        perror("bind failed");
        return;
    }

    // Continuous loop to process "DOOR" UDP datagrams
    while (1) {
        printf("Waiting on port (specify your port)...\n");
        socklen_t addrlen = sizeof(myaddr); // length of addresses
        recvlen = recvfrom(udp_sockfd, buf, BUFFER_SIZE, 0, (struct sockaddr *)&myaddr, &addrlen);

        if (recvlen > 0) {
            buf[recvlen] = 0; // null terminate the buffer
            printf("Received message: \"%s\"\n", buf);

            // Check if it's a "DOOR" message
            door_datagram *door_data = (door_datagram *)buf;
            if (memcmp(door_data->header, "DOOR", 4) == 0) { // "DOOR" header indicates a door message
                // Respond to the door message, you may need to extract IP and port from the received message.
                struct in_addr door_ip = door_data->door_addr; 
                in_port_t door_port = door_data->door_port;

                // Send emergency open command
                send_open_emergency_to_door(door_ip, door_port);

                // Send back a confirmation to the door system
                send_door_confirmation(door_ip, door_port);
            }
        }
        else if (recvlen < 0) {
            perror("Error in recvfrom");
        }
    }

    // Ideally, should never reach this point in an infinite loop
    close(udp_sockfd); // Don't forget to close the socket at the end
}