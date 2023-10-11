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

typedef struct {
    float temperature;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shm_sensor;

int main(int argc, char **argv[]) {
    if (argc<6) {
        fprintf(stderr, "usage: {id} {address:port} {max condvar wait (microseconds)} {max update wait (microseconds)} {shared memory path} {shared memory offset} {receiver address:port}...");
        exit(1);
    }

    // intialise parameters for system 
    int id = atoi(argv[1]);
    const char *tempsensor_addr = argv[2];
    int max_wait_condvar = atoi(argv[3]);
    int max_wait_update = atoi(argv[4]);
    const char *shm_path = argv[5];
    off_t shm_offset = (off_t)atoi(argv[6]);
    //create array of all receiver addresses that are supplied

    //UDP CONNECTION FUNCTION

    //Shared memory
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
    shm_sensor *shared = (shm_sensor *)(shm + shm_offset);

    // mutex lock for normal operation
    pthread_mutex_lock(&shared->mutex);
    float oldTemp = shared->temperature;
    int firstIteration = 1; // see if this is first iteration of for loop. 1 for true, 0 for false
    for(;;) {
        float currentTemp = shared->temperature;
        pthread_mutex_unlock(&shared->mutex);
        if (currentTemp != oldTemp || firstIteration == 1) {  // or if max update wait has passed since sensor last sent an update 
           
        }
    }

    return 0;
}