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

struct {
    char status; // '-' for inactive, '*' for active
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shm_callpoint;

int main(int argc, char **argv) 
{
    // see if enough arguments were supplied for this program
    if (argc!=5) {
        fprintf(stderr, "usage: {resend delay (in microseconds)} {shared memory path} {shared memory offset} {fire alarm unit address:port}");
        exit(1);
    }

    // intialise parameters for system by converting from char[] to int when necessary
    int delay = atoi(argv[1]);
    const char *shm_path = argv[2];
    off_t shm_offset = (off_t)atoi(argv[3]);
    const char *fire_alarm_addr = argv[5];

    return 0;
}