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

#define BUFFER_SIZE 16
#define RECEIVED_BUFFER_SIZE 1024

#include <tcp_communication.h>

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
    int id = atoi(argv[1]);
    int waitTime = atoi(argv[2]);
    const char *shm_path = argv[3];
    off_t shm_offset = (off_t)atoi(argv[4]);
    const char *overseer_port = argv[5]; // temporary variable type

    /**************************
    Code to connect to overseer
    **************************/

    // Initialise TCP connection to overseer

    // Define server address and port
    struct sockaddr_in serverAddr;
    configureServerAddress(&serverAddr, "127.0.0.1", *overseer_port);

    // Create socket
    int sockfd = createSocket();
    if (sockfd == 1) {
        exit(1);
    }
 
    // Establish connection and corresponding error handling
    int connection_status = establishConnection(sockfd, &serverAddr);
    if (connection_status == 1) {
        exit(1);
    }

    // Initialisation message to overseer
    char helloMessage[256];
    snprintf(helloMessage, sizeof(helloMessage), "CARDREADER %s HELLO#", id);
    int sent = sendData(sockfd, helloMessage);
    if (connection_status == 1) {
        exit(1);
    }

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
            char buf[BUFFER_SIZE+1];
            char scannedMessage[50];
            memcpy(buf, shared->scanned,16);
            buf[16] = '\0';

            // SEND SCANNED DATA
            snprintf(scannedMessage, sizeof(scannedMessage), "CARDREADER %d SCANNED %s#", id, shared->scanned);
            int sendMessage = send(sockfd, scannedMessage, strlen(scannedMessage), 0);
            if(sendMessage == -1) {
                perror("send()");
            }

            /*****************************************
            ACT ACCORDING TO HOW OVERSEER RESPONDS
            //****************************************/

            // Logic to recieve data
            char receiveBuf[RECEIVED_BUFFER_SIZE];
            int messageReceived = recv(sockfd, receiveBuf, sizeof(receiveBuf), 0);
            // Logic to see handle error or connection close
            if (messageReceived == -1 || messageReceived == 0) {
                shared->response = 'N';
            }
            // Logic to process data from server
            else {
                receiveBuf[messageReceived] = '\0'; // Null terminate received data
                if (receiveBuf[0] == 'Y') {
                    shared->response = 'Y';
                }
                else {
                    shared->response = 'N';
                }
            }
            printf("Scanned %s\n", buf);
            shared->response = 'Y';
            pthread_cond_signal(&shared->response_cond);
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

    close(sockfd);
    close(shm_fd);

    return 0;

}
