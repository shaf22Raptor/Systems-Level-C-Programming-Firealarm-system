/*
 *This is the main executable file for the callpoint program, a safety-critical application. 
 *Under normal operation, the program will constantly check its state as to whether or not it has been activated,
 *when its state is '*' or '-' in shared memory with the simulator. 
 *If its state is represented by '*', it will constantly send a udp datagram containing the message 'FIRE' to the 
 *firealarm whose address:port is supplied as a command line argument.
 *
 *pointers used on command line arguments and certain shared memory constructs
*/
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

/* Message for emergency Datagram */
struct Data {
    char header[4]; /* {F,I,R,E} */
};

struct shm_callpoint{
    char status; /* '-' for inactive, '*' for active */
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} ;

/* Main loop of program. Sets up UDP connection with firealarm and shared memory system with simulator 
 *before commencing normal operation.
*/
int main(int argc, char **argv) 
{
    /*Initialise datagram that contains the message FIRE*/
    struct Data fire;
    strncpy(fire.header, "FIRE", sizeof(fire.header));

    /* see if enough arguments were supplied for this program */
    if (argc!=5) {
        fprintf(stderr, "usage: {resend delay (in microseconds)} {shared memory path} {shared memory offset} {fire alarm unit address:port}");
        exit(1);
    }

    /* intialise parameters for system by converting from char[] to int when necessary */
    const int resendDelay = atoi(argv[1]);
    const char *shm_path = argv[2];
    off_t shm_offset = (off_t)atoi(argv[3]);
    const char *firealarm_address_port = argv[4];

    /* Isolate port number from {ipAddress : port number} */
    const char *portString= strstr(firealarm_address_port , ":");
    const int portNumber = atoi(portString + 1);

    /* initialise shared memory */
    int shm_fd = shm_open(shm_path, O_RDWR, 0);
    if (shm_fd == -1) {
        perror("shm_open()");
        exit(1);
    }

    /* Obtain statistics related to shm. Intended to find size of shm. */
    struct stat shm_stat;
    if (fstat(shm_fd, &shm_stat) == -1) {
        perror("fstat()");
        exit(1);
    }

    /* mmap */
    char *shm = mmap(NULL, shm_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap()");
        exit(1);
    }

    /* cast memory offset onto card_reader // check this area for errors */
    struct shm_callpoint *shared = (struct shm_callpoint *)(shm + shm_offset);
    
    /* Initialise UDP connection to fire alarm unit */
    /* Create UDP socket */
    int udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sockfd == -1) { 
        perror("\nsocket()\n");
        return 1;
    }
    /* Define server address and port */
    struct sockaddr_in firealarmAddr;
    firealarmAddr.sin_family = AF_INET;
    firealarmAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    firealarmAddr.sin_port = htons(portNumber);

    /* mutex lock for normal operation */
    int mutex_lock_result = pthread_mutex_lock(&shared->mutex);
    if(mutex_lock_result != 0) {
        perror("pthread_mutex_lock()");
        exit(1);
    }
    
    /*main loop. Checks if */ 
    for(;;) {
        /*Checks if callpoint has been activated. '*' for activated, '-' for not activated.*/
        if (shared->status == '*') {
            for(;;) {
                /* send emergency alarm */
                ssize_t send_result = (sendto(udp_sockfd, &fire, sizeof(fire), 0, (struct sockaddr *) &firealarmAddr, sizeof(firealarmAddr)));
                if (send_result == -1) {
                    perror("sendto()");
                    exit(1);
                }

                usleep(resendDelay);
                
            }
        }
        pthread_cond_wait(&shared->cond, &shared->mutex);
    }
    pthread_mutex_unlock(&shared->mutex);
    
    /*general cleanup with error handling */
    if (shm_unlink(shm_path) == -1) {
        perror("shm_unlink()");
        exit(1);
    }   

    if(pthread_mutex_destroy(&shared->mutex) !=0) {
        perror("pthread_mutex_destroy()");
        exit(1);
    }

    if(pthread_cond_destroy(&shared->cond) != 0) {
        perror("pthread_cond_destroy");
        exit(1);
    }

    if(pthread_cond_destroy(&shared->status) != 0) {
        perror("pthread_cond_destroy");
        exit(1);
    }
    
    if (munmap(shm, shm_stat.st_size) == -1) {
        perror("munmap()");
        exit(1);
    }

    /*close udp socket*/
    if (close(udp_sockfd) == -1) {
        perror("close(udp_sockfd)");
        exit(1);
    }

    /*close shared memory*/
    if(close(shm_fd) == -1) {
        perror("close(shm_fd)");
        exit(1);
    }

    return 0;
}