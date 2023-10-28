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
int overseer_sock; // Global variable for the overseer socket connection
struct sockaddr_in overseer_addr;

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
    char *udp_addr_port = argv[1]; 
    struct sockaddr_in udp_servaddr;
    memset(&udp_servaddr, 0, sizeof(udp_servaddr)); // Ensure struct is empty

    // Set properties for the address structure
    udp_servaddr.sin_family = AF_INET;
    char *udp_ip = strtok(udp_addr_port, ":");
    char *udp_port_str = strtok(NULL, ":");
    if (udp_port_str == NULL) {
        fprintf(stderr, "Invalid overseer address:port format.\n");
        exit(EXIT_FAILURE);
    }
    int udp_port = atoi(udp_port_str); // Convert port from string to integer
    udp_servaddr.sin_port = htons(udp_port); // Convert port number to network byte order

    // Validate and convert IP address from text to binary form
    if (inet_aton(udp_ip, &udp_servaddr.sin_addr) == 0) { 
        fprintf(stderr, "Invalid IP address format for UDP.\n");
        exit(EXIT_FAILURE);
    }

    // UDP socket creation (SOCK_DGRAM for UDP)
    int udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
    if (udp_sockfd < 0) {
        perror("Cannot create UDP socket");
        return EXIT_FAILURE;
    }

    printf("UDP Socket created \n"); // Debug message

    // Binding the UDP socket to the local address and port
    if (bind(udp_sockfd, (struct sockaddr*)&udp_servaddr, sizeof(udp_servaddr)) < 0) {
        perror("bind failed for UDP socket");
        exit(EXIT_FAILURE);
    }

    printf("UDP Socket bound to address\n"); // debug message

    /* Connect to overseer and send initialisation message */
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
    overseer_sock = socket(AF_INET, SOCK_STREAM, 0);
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
    snprintf(init_message, sizeof(init_message), "FIREALARM %s:%d HELLO#", udp_ip, udp_port);
    
    // Send the initialisation message to the overseer
    ssize_t bytes_sent = send(overseer_sock, init_message, strlen(init_message), 0);
    if (bytes_sent < 0) {
        perror("Failed to send initialisation message to overseer");
        close(overseer_sock);  // Close the overseer socket
        exit(EXIT_FAILURE);
    }

    printf("Initialisation message sent to overseer\n"); // debug message
 
    // Main loop
    while (1) {
        char buffer[BUFFER_SIZE];  // Buffer for incoming data.
        memset(buffer, 0, BUFFER_SIZE); // Clearing the buffer

        struct sockaddr_in remote_addr; // Address structure for the remote sender.
        socklen_t addr_len = sizeof(remote_addr); // Variable for address length.

        // Receiving a datagram
        ssize_t rec_size = recvfrom(udp_sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&remote_addr, &addr_len);
        if (rec_size < 0) {
            perror("recvfrom() failed");
            continue; 
        }  
        
        else if (rec_size == 0) {
            // This block will handle the scenario where no data is received.
            printf("No data received.\n");
            continue; 
        }

        // Check if it's a DOOR datagram
        door_datagram *door_data = (door_datagram *)buffer;
        if (memcmp(door_data->header, "DOOR", 4) == 0) { // Check for the correct header
            printf("Received DOOR datagram. Sending confirmation\n"); //debug message 

            // Construct the DREG (confirmation) message
            door_confirmation confirmation;
            memcpy(confirmation.header, "DREG", 4); // Copy 'DREG' to the header
            confirmation.door_addr = door_data->door_addr; // Copying the door IP
            confirmation.door_port = door_data->door_port; // Copying the door port

            // Send DREG to the overseer
            ssize_t sent_size = sendto(udp_sockfd, &confirmation, sizeof(confirmation), 0, 
                                        (struct sockaddr*)&overseer_addr, sizeof(overseer_addr));
            if (sent_size < 0) {
                perror("sendto(overseer) failed");
                continue; 
            }
            printf("Sent DREG confirmation to overseer.\n"); //debug message 
        } 
        
        // Check if it's a FIRE datagram
        else if (memcmp(buffer, "FIRE", 4) == 0) { // Check for the correct header
            printf("Received FIRE datagram. Activating alarm...\n");

            // Lock the mutex before modifying the shared data
            pthread_mutex_lock(&(shared->mutex));

            // Set 'alarm' to 'A'
            shared->alarm = 'A';

            // Unlock the mutex
            pthread_mutex_unlock(&(shared->mutex));

            // Signal the condition variable 
            pthread_cond_signal(&(shared->cond));

            // Communicate with each registered door
            for (int i = 0; i < door_count; ++i) {
                struct sockaddr_in door_addr;
                memset(&door_addr, 0, sizeof(door_addr));
                door_addr.sin_family = AF_INET;
                door_addr.sin_addr = door_addresses[i];
                door_addr.sin_port = door_ports[i];

                // Create a new socket for TCP connection
                int door_sock = socket(AF_INET, SOCK_STREAM, 0);
                if (door_sock < 0) {
                    perror("Cannot create socket");
                    continue;  // If a socket fails, continue to try the others
                }

                // Connect to the door
                if (connect(door_sock, (struct sockaddr*)&door_addr, sizeof(door_addr)) < 0) {
                    perror("Connection to door failed");
                    close(door_sock);
                    continue;  // If a connection fails, continue to try the others
                }

                // Send OPEN_EMERG# command to the door
                char command[] = "OPEN_EMERG#";
                ssize_t sent_bytes = send(door_sock, command, sizeof(command), 0);
                if (sent_bytes < 0) {
                    perror("Failed to send command to door");
                }

                // Close the connection
                close(door_sock);
            }
            printf("Alarm activated, all doors notified.\n");
        } 
        
        else if (memcmp(buffer, "TEMP", 4) == 0) {
            printf("Received TEMP datagram.\n");

            // Parse the datagram content
            struct datagram_format *temp_datagram = (struct datagram_format *)buffer;
            
            // Check if the temperature is above the threshold and the data is recent
            if (temp_datagram->temperature >= temp_threshold) {
                // Get current time
                struct timeval current_time;
                gettimeofday(&current_time, NULL);
                long long current_timestamp = (long long)current_time.tv_sec * 1000000 + current_time.tv_usec;

                // Check the age of the detection
                if ((current_timestamp - (long long)temp_datagram->timestamp.tv_sec * 1000000 - temp_datagram->timestamp.tv_usec) <= detection_period) {
                    // Remove old detections
                    int i = 0;
                    while (i < detection_count) {
                        if ((current_timestamp - detection_timestamps[i]) > detection_period) {
                            // Remove the old detection by shifting the later entries forward
                            for (int j = i; j < detection_count - 1; j++) {
                                detection_timestamps[j] = detection_timestamps[j + 1];
                            }
                            detection_count--;
                        } else {
                            i++;
                        }
                    }

                    // Add new detection timestamp
                    if (detection_count < MAX_DETECTIONS) {
                        detection_timestamps[detection_count++] = (long long)temp_datagram->timestamp.tv_sec * 1000000 + temp_datagram->timestamp.tv_usec;
                    }

                    // Check if sufficient detections are met to trigger an alarm
                    if (detection_count >= min_detections) {
                        printf("Temperature threshold exceeded. Activating alarm...\n");

                        // Lock the mutex before modifying the shared data
                        pthread_mutex_lock(&(shared->mutex));

                        // Set 'alarm' to 'A'
                        shared->alarm = 'A';

                        // Unlock the mutex
                        pthread_mutex_unlock(&(shared->mutex));

                        // Signal the condition variable 
                        pthread_cond_signal(&(shared->cond));

                        // Communicate with each registered door
                        for (int i = 0; i < door_count; ++i) {
                            struct sockaddr_in door_addr;
                            memset(&door_addr, 0, sizeof(door_addr));
                            door_addr.sin_family = AF_INET;
                            door_addr.sin_addr = door_addresses[i];
                            door_addr.sin_port = door_ports[i];

                            // Create a new socket for TCP connection
                            int door_sock = socket(AF_INET, SOCK_STREAM, 0);
                            if (door_sock < 0) {
                                perror("Cannot create socket");
                                continue;  // If a socket fails, continue to try the others
                            }

                            // Connect to the door
                            if (connect(door_sock, (struct sockaddr*)&door_addr, sizeof(door_addr)) < 0) {
                                perror("Connection to door failed");
                                close(door_sock);
                                continue;  // If a connection fails, continue to try the others
                            }

                            // Send OPEN_EMERG# command to the door
                            char command[] = "OPEN_EMERG#";
                            ssize_t sent_bytes = send(door_sock, command, sizeof(command), 0);
                            if (sent_bytes < 0) {
                                perror("Failed to send command to door");
                            }

                            // Close the connection
                            close(door_sock);
                        }

                        printf("Alarm activated, all doors notified.\n");
                    }
                }
            }
        }

        else {
            printf("Received unknown datagram.\n");
        }
    }
    munmap(shm, shm_stat.st_size);
    close(udp_sockfd); // UDP socket for fire alarm system
    close(overseer_sock); // TCP socket for communication with the overseer
    return 0;  // Successful exit
}