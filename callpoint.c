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

#include "udp_communication.h"

// Message for emergency Datagram
char header[4] = "FIRE";

typedef struct {
    char status; // '-' for inactive, '*' for active
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shm_callpoint;

int main(int argc, char **argv) 
{
    printf("\nentered main function\n");
    // see if enough arguments were supplied for this program
    if (argc!=5) {
        fprintf(stderr, "usage: {resend delay (in microseconds)} {shared memory path} {shared memory offset} {fire alarm unit address:port}");
        exit(1);
    }
    printf("\n checked supplied arguments\n");
    // intialise parameters for system by converting from char[] to int when necessary
    int resendDelay = atoi(argv[1]);
    const char *shm_path = argv[2];
    off_t shm_offset = (off_t)atoi(argv[3]);
    const char *firealarm_address_port = argv[4];
    printf("\ncollected command line arguments\n");

    // Isolate port number from {ipAddress : port number}
    const char *portString= strstr(firealarm_address_port , ":");
    int portNumber = atoi(portString + 1);
    printf("\ngot portnumber\n");

    /*********************************************
    Code to connect to share memory with simulator
    *********************************************/

    // initialise shm
    int shm_fd = shm_open(shm_path, O_RDWR, 0);
    printf("\nopen shared memory\n");

    // handle failed shm_open
    if (shm_fd == -1) {
        perror("shm_open()");
        exit(1);
    }
    printf("\nshared memory fine\n");
    // Obtain statistics related to shm. Intended to find size of shm.
    struct stat shm_stat;

    if (fstat(shm_fd, &shm_stat) == -1) {
        perror("fstat()");
        exit(1);
    }
    printf("\nstat check fine\n");
    // mmap 
    char *shm = mmap(NULL, shm_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    printf("\nmmap performed\n");
    if (shm == MAP_FAILED) {
        perror("mmap()");
        exit(1);
    }
    printf("\nmmap fine\n");

    // cast memory offset onto card_reader
    shm_callpoint *shared = (shm_callpoint *)(shm + shm_offset);
    printf("\n shared identifier made\n");
    
    // Initialise UDP connection to fire alarm unit

    // mutex lock for normal operation
    pthread_mutex_lock(&shared->mutex);

    for(;;) {
        printf("\nentered infinite for loop\n");
        printf("\nstatus of callpoint should be written below\n");
        printf("\nstatus of callpoint is %c\n", shared->status);
        if (shared->status == '-') {
            printf("\nreceived %s as message\n", shared->status);
            //for(;;) {
                /********************
                SEND EMERGENCY ALARM
                //******************/


                /****************************************
                 * code to connect to fire alarm
                ****************************************/

                // Create UDP socket
            /*    int udp_sockfd = createSocket();
                printf("\ncreated socket\n");
                // Define server address and port
                struct sockaddr_in firealarmAddr;
                //configureServerAddress(&firealarmAddr, "127.0.0.1", firealarm_port);
                firealarmAddr.sin_family = AF_INET;
                firealarmAddr.sin_port = htons(portNumber);
                firealarmAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
                printf("\nconfigured connection settings\n");

                printf("\nEntered 2nd infinite for loop\n");
                int emergencyAlarm = sendData(udp_sockfd, header, &firealarmAddr);
                printf("\nsent fire alarm message\n");
                close(udp_sockfd);
                usleep(resendDelay);
                */
            //}
        }
        pthread_cond_wait(&shared->status, &shared->mutex);
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

    if(pthread_cond_destroy(&shared->status) != 0) {
        perror("pthread_cond_destroy");
        exit(1);
    }

    if(pthread_cond_destroy(&shared->status) != 0) {
        perror("pthread_cond_destroy");
        exit(1);
    }
    
    if (munmap(shm, shm_stat.st_size) == -1) {
        perror("munmap()");
    }

    close(shm_fd);

    return 0;
}