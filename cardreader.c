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
#include <arpa/inet.h>
#include <netinet/in.h>
#define BUFFER_SIZE 1023
// inprogess change

// Struct used for card reader shared memory as specified
typedef struct {
    char scanned[16];
    pthread_mutex_t mutex;
    pthread_cond_t scanned_cond;

    char response; // 'Y' or 'N' (or '\0' at first)
    pthread_cond_t response_cond;

} shm_cardreader;

int main(int argc, char **argv) 
{
    // see if enough arguments were supplied for this program
    if (argc<6) {
        fprintf(stderr, "usage: {id} {wait time (in microseconds)} {shared memory path} {shared memory offset} {overseer address:port}");
        exit(1);
    }

    // intialise parameters for system by converting from char[] to int when necessary
    int id = atoi(argv[1]);
    int waitTime = atoi(argv[2]);
    const char *shm_path = argv[3];
    int shm_offset = argv[4];
    const char *overseer_addr = argv[5]; // temporary variable type

    /**************************
    Code to connect to overseer
    **************************/

    // Initialise TCP connection to overseer
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);   // Create socket for client and corresponding error handling
    if (sockfd == -1) { 
        perror("\nsocket()\n");
        return 1;
    }
 
    // Define server address and port
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(&overseer_addr);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // Establish connection and corresponding error handling
    int connection_status = connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (connection_status == -1) {
        printf("Error: Connection to the server failed\n");
        exit(1);
    }
    // Initialisation message to overseer
    char helloMessage[256];
    snprintf(helloMessage, sizeof(helloMessage), "CARDREADER %s HELLO#", id);
    send(sockfd, helloMessage, strlen(helloMessage), 0);
    shutdown(sockfd, SHUT_RDWR);

    /*********************************************
    Code to connect to share memory with simulator
    *********************************************/

    // initialise shm
    int shm_fd = shm_open(shm_path, O_RDWR, 0);

    // handle failed shm_open
    if (shm_fd == -1) {
        perror("shm_open()");
        exit(1);
    }

    // Obtain statistics related to shm. Intended to find size of shm.
    struct stat shm_stat;

    if (fstat(shm_fd, &shm_stat) == -1) {
        perror("fstat()");
        exit(1);
    }

    printf("Shared memory file size: %ld\n", shm_stat.st_size);

    // mmap 
    char *shm = mmap(NULL, shm_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (shm == MAP_FAILED) {
        perror("mmap()");
        exit(1);
    }

    // cast memory offset onto card_reader
    shm_cardreader *shared = (shm_cardreader *)(shm + shm_offset);

    // mutex lock for normal operation
    pthread_mutex_lock(&shared->mutex);

    for(;;) {
        if (shared->scanned[0] != '\0') {
            char buf[17];
            char scannedMessage[50];
            memcpy(buf, shared->scanned,16);
            buf[16] = '\0';
            // OPEN TCP CONNECTION TO SERVER
            int connection_status = connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
            if (connection_status == -1) {
                printf("Error: Connection to the server failed\n");
                exit(1);
            }
            // SEND SCANNED DATA
            snprintf(scannedMessage, sizeof(scannedMessage), "CARDREADER %d SCANNED %s#", id, buf);
            send(sockfd, scannedMessage, strlen(scannedMessage), 0);
            shutdown(sockfd, SHUT_RDWR);
            // ACT ACCORDING TO HOW OVERSEER RESPONDS
            /*
            if (response == ALLOWED#) {
                response = Y;
            }
            else {
                response = N;
            }
            */
            printf("Scanned %s\n", buf);
            shared->response = 'Y';
            pthread_cond_signal(&shared->response_cond);
        }

        pthread_cond_wait(&shared->scanned_cond, &shared->mutex);

    }
    
    // close when done
    close(sockfd);
    close(shm_fd);

    return 0;

}
