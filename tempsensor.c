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

struct timeval lastUpdateTime;

void updateLastUpdateTime() {
    gettimeofday(&lastUpdateTime, NULL);
}

// Function to check if the maximum update wait time has passed
int hasMaxWaitTimePassed(int maxUpdateWait) {
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);

    // Calculate the time difference in microseconds
    long int diff = (currentTime.tv_sec - lastUpdateTime.tv_sec) * 1000000 + (currentTime.tv_usec - lastUpdateTime.tv_usec);

    // Compare with the maximum update wait time
    if (diff > maxUpdateWait) {
        // If the time difference is greater than the max update wait, return 1
        return 1;
    } else {
        // If the time difference is less than the max update wait, return 0
        return 0;
    }
}

typedef struct {
    float temperature;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shm_sensor;

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
        firstIteration = 0;
        float currentTemp = shared->temperature;
        pthread_mutex_unlock(&shared->mutex);
        if (currentTemp != oldTemp || firstIteration == 1 || hasMaxWaitTimePassed(max_wait_update)) {
            firstIteration = 0; 
           // construct a struct that contains sensor's id, temp and current time and address list of only this sensor
           
           // send datagram to each receiver
        }
        
    }

    return 0;
}