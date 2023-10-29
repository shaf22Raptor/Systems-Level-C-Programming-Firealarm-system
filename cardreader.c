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
#include "tcp_communication.h"

#define BUFFER_SIZE 16
#define RECEIVED_BUFFER_SIZE 1024

const char programName[] = "cardreader";

// Struct used for card reader shared memory as specified
typedef struct {
    char scanned[BUFFER_SIZE];
    pthread_mutex_t mutex;
    pthread_cond_t scanned_cond;
    char response; // 'Y' or 'N' (or '\0' at first)
    pthread_cond_t response_cond;
} shm_cardreader;

int main(int argc, char **argv) 
{
    // see if enough arguments were supplied for this program
    if (argc!=6) {
        fprintf(stderr, "usage: {id} {wait time (in microseconds)} {shared memory path} {shared memory offset} {overseer address:port}");
        exit(1);
    }
    // intialise parameters for system by converting from char[] to int when necessary
    const int id = atoi(argv[1]);
    const int waitTime = atoi(argv[2]);
    const char *shm_path = argv[3];
    const off_t shm_offset = (off_t)atoi(argv[4]);
    const char *overseer_port = argv[5]; // temporary variable type

    // Isolate port number from {ipAddress : port number}
    const char *portString= strstr(overseer_port, ":");
    const int portNumber = atoi(portString + 1);

    /*********************************************
    Code to connect to shared memory with simulator
    *********************************************/

    // initialise shm
    int shm_fd = shm_open(shm_path, O_RDWR, 0);
   // printf("\n\nshm_open executed\n");

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
 //   printf("Shared memory file size: %ld\n", shm_stat.st_size);

    // mmap 
    char *shm = mmap(NULL, shm_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (shm == MAP_FAILED) {
        perror("mmap()");
        exit(1);
    }

    shm_cardreader *shared = (shm_cardreader *)(shm+shm_offset);

    /**************************
    Code to connect to overseer
    **************************/
     // Create socket
    int sockfd = createSocket();
    if (sockfd == 1) {
        printf("socket creation failed");
        exit(1);
    }

    // Define server address and port
    struct sockaddr_in serverAddr;
    configureServerAddressForClient(serverAddr, "127.0.0.1");

    // Establish connection and corresponding error handling
    establishConnection(sockfd, serverAddr, portNumber);

    // Initialisation message to overseer
    char helloMessage[50];
    sprintf(helloMessage, "CARDREADER %d HELLO#", id);
    sendData(sockfd, helloMessage);

    if (shutdown(sockfd, SHUT_RDWR) < 0) {
        perror("Error in shutting down");
        return 1; // Return an error code if shutdown fails
    }

    close(sockfd);

    // mutex lock for normal operation
    pthread_mutex_lock(&shared->mutex);
    //printf("\n mutex lock done\n");


    for(;;) {
        if (shared->scanned[0] != '\0') {

            int sockfd2 = createSocket();

            // Define server address and port
            struct sockaddr_in serverAddr;
            configureServerAddressForClient(serverAddr, "127.0.0.1");

            // Establish connection and corresponding error handling
            establishConnection(sockfd2, serverAddr, portNumber);

            char scannedMessage[50];

            // SEND SCANNED DATA
            sprintf(scannedMessage, "CARDREADER %d SCANNED %s#", id, shared->scanned);
            sendData(sockfd2, scannedMessage);

            /*****************************************
            ACT ACCORDING TO HOW OVERSEER RESPONDS
            //****************************************/

            // Logic to recieve data
            char receiveBuf[RECEIVED_BUFFER_SIZE];
            int messageReceived = receiveData(sockfd, receiveBuf);
            // Logic to see handle error or connection close
            if (messageReceived == -1 || messageReceived == 0) {
                shared->response = 'N';
            }
            // Logic to process data from server
            else {
                receiveBuf[messageReceived] = '\0'; // Null terminate received data
                if (strcmp(receiveBuf, "ALLOWED#") == 0) {
                    shared->response = 'Y';
                }
                else {
                    shared->response = 'N';
                }
            }
            pthread_cond_signal(&shared->response_cond);

            if (shutdown(sockfd, SHUT_RDWR) < 0) {
                perror("Error in shutting down");
            return 1; // Return an error code if shutdown fails
            }
            close(sockfd2);
        }
        pthread_cond_wait(&shared->scanned_cond, &shared->mutex);
    }

    pthread_mutex_unlock(&shared->mutex);

    

    //general cleanup with error handling
    if (shm_unlink(shm_path) == -1) {
        perror("shm_unlink()");
        exit(1);
    }   

    if(pthread_mutex_destroy(&shared->mutex) !=0) {
        perror("pthread_mutex_destroy()");
        exit(1);
    }

    if(pthread_cond_destroy(&shared->scanned_cond) != 0) {
        perror("pthread_cond_destroy");
        exit(1);
    }

    if(pthread_cond_destroy(&shared->response_cond) != 0) {
        perror("pthread_cond_destroy");
        exit(1);
    }

    if (munmap(shm, shm_stat.st_size) == -1) {
        perror("munmap()");
    }

    close(shm_fd);

    return 0;
}
