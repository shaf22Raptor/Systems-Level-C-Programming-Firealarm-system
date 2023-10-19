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

#include <udp_communication.h>

// Message for emergency Datagram
char header[4] = "FIRE";

typedef struct {
    char status; // '-' for inactive, '*' for active
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shm_callpoint;


/// @brief Safety Critical system
/// @param argc 
/// @param argv 
/// @return 
int main(int argc, char **argv) 
{
    // see if enough arguments were supplied for this program
    if (argc!=5) {
        fprintf(stderr, "usage: {resend delay (in microseconds)} {shared memory path} {shared memory offset} {fire alarm unit address:port}");
        exit(1);
    }

    // intialise parameters for system by converting from char[] to int when necessary
    int resendDelay = atoi(argv[1]);
    const char *shm_path = argv[2];
    off_t shm_offset = (off_t)atoi(argv[3]);
    const char *firealarm_port = argv[5];

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
    shm_callpoint *shared = (shm_callpoint *)(shm + shm_offset);

    /****************************************
     * code to connect to fire alarm
    ****************************************/

    // Define server address and port
    struct sockaddr_in firealarmAddr;
    configureServerAddress(&firealarmAddr, "127.0.0.1", firealarm_port);

    // Initialise UDP connection to fire alarm unit
    int udp_sockfd = createSocket();

    // mutex lock for normal operation
    pthread_mutex_lock(&shared->mutex);

    for(;;) {
        if (shared->status == '*') {
            for(;;) {
                /********************
                SEND EMERGENCY ALARM
                //******************/
                ssize_t emergencyAlarm = sendData(udp_sockfd, shared->status, &firealarmAddr);
                usleep(resendDelay);
            }
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

    close(udp_sockfd);
    close(shm_fd);

    return 0;
}