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
void updateLastUpdateTime();
int hasMaxWaitTimePassed(int maxUpdateWait);

typedef struct
{
    float temperature;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shm_sensor;

struct addr_entry
{
    struct in_addr sensor_addr;
    in_port_t sensor_port;
};

struct datagram_format
{
    char header[4]; // {'T', 'E', 'M', 'P'}
    struct timeval timestamp;
    float temperature;
    uint16_t id;
    uint8_t address_count;
    struct addr_entry address_list[50];
};

int main(int argc, char **argv[])
{
    if (argc < 6)
    {
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

    const char *portString = strstr(tempsensor_addr, ":");
    int portNumber = atoi(portString + 1);

    // Shared memory
    //  initialise shm
    int shm_fd = shm_open(shm_path, O_RDWR, 0);

    // handle failed shm_open
    if (shm_fd == -1)
    {
        perror("shm_open()");
        exit(1);
    }

    // Obtain statistics related to shm. Intended to find size of shm.
    struct stat shm_stat;

    if (fstat(shm_fd, &shm_stat) == -1)
    {
        perror("fstat()");
        exit(1);
    }

    printf("Shared memory file size: %ld\n", shm_stat.st_size);

    // mmap
    char *shm = mmap(NULL, shm_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (shm == MAP_FAILED)
    {
        perror("mmap()");
        exit(1);
    }

    // cast memory offset onto card_reader
    shm_sensor *shared = (shm_sensor *)(shm + shm_offset);

    // Create a socket
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // mutex lock for normal operation
    pthread_mutex_lock(&shared->mutex);
    float oldTemp = shared->temperature;
    int firstIteration = 1; // see if this is first iteration of for loop. 1 for true, 0 for false
    for (;;)
    {
        firstIteration = 0;
        float currentTemp = shared->temperature;
        pthread_mutex_unlock(&shared->mutex);
        if (currentTemp != oldTemp || firstIteration == 1 || hasMaxWaitTimePassed(max_wait_update))
        {
            firstIteration = 0;
            // construct a struct that contains sensor's id, temp and current time and address list of only this sensor
            struct datagram_format datagram;

            // construct header
            strcpy(datagram.header, "TEMP");

            // timestamp
            struct timeval timeStamp;
            gettimeofday(&timeStamp, NULL);

            // temparture
            datagram.temperature = currentTemp;

            // id
            datagram.id = id;

            // address count
            datagram.address_count = 1;

            // list of addresses. It should only contain this sensor

            // Create addr_entry instance containing this sensor's details
            struct addr_entry thisSensor;
            if (inet_pton(AF_INET, tempsensor_addr, &(thisSensor.sensor_addr)) <= 0)
            {
                perror("Invalid address");
                return 1; // Handle the error appropriately
            }
            thisSensor.sensor_port = portNumber;

            // Add this sensor's details to the list
            datagram.address_list[0] = thisSensor;
            
            //  send datagram to each receiver
            for (int i = 7; i <= argc; i++) {
                const char *address_port = argv[i];
                const char *receiverPortString = strstr(address_port, ":");
                int receiverPortNumber = atoi(receiverPortString + 1);

                struct sockaddr_in receiver_addr;
                receiver_addr.sin_family = AF_INET;
                receiver_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                receiver_addr.sin_port = htons(receiverPortNumber);

                if (inet_pton(AF_INET, argv[i], &receiver_addr.sin_addr) <= 0) {
                    perror("Invalid address");
                    exit(EXIT_FAILURE);
                }

                if (sendto(sockfd, &datagram, sizeof(datagram), 0, (struct sockaddr *) &receiver_addr, sizeof(receiver_addr)) == -1) {
                    perror("sendto failed");
                    exit(1);
                }
            }
        }
        while(1) {

        }
    }

    return 0;
}

void updateLastUpdateTime()
{
    gettimeofday(&lastUpdateTime, NULL);
}

// Function to check if the maximum update wait time has passed
int hasMaxWaitTimePassed(int maxUpdateWait)
{
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);

    // Calculate the time difference in microseconds
    long int diff = (currentTime.tv_sec - lastUpdateTime.tv_sec) * 1000000 + (currentTime.tv_usec - lastUpdateTime.tv_usec);

    // Compare with the maximum update wait time
    if (diff > maxUpdateWait)
    {
        // If the time difference is greater than the max update wait, return 1
        return 1;
    }
    else
    {
        // If the time difference is less than the max update wait, return 0
        return 0;
    }
}