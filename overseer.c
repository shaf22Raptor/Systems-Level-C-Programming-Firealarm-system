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
#include <sys/time.h>
#include <time.h>

typedef struct {
    char security_alarm; // '-' if inactive, 'A' if active
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shm_sensor;

int main(int argc, char **argv[])
{
    if (argc < 8)
    {
        fprintf(stderr, "usage: {address:port} {door open duration (in microseconds)} {datagram resend delay (in microseconds)} {authorisation file} {connections file} {layout file} {shared memory path} {shared memory offset}");
        exit(1);
    }
    const char *overseer_addr = argv[1];
    int doorOpenDuration = atoi(argv[2]);
    int dGramResendDelay = atoi(argv[3]);
    const char *shm_path = argv[5];
    off_t shm_offset = (off_t)atoi(argv[6]);
}



