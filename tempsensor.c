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

#define MAX_BUFFER_SIZE 1024

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

int main(int argc, char **argv)
{
    if (argc < 6)
    {
        fprintf(stderr, "usage: {id} {address:port} {max condvar wait (microseconds)} {max update wait (microseconds)} {shared memory path} {shared memory offset} {receiver address:port}...");
        exit(1);
    }

    struct sockaddr_in sensor_addr, receiver_addr, client_addr;

    struct datagram_format datagram, receivedDatagram;
    socklen_t addr_size;

    char receiveBuffer[MAX_BUFFER_SIZE];

    // intialise parameters for system
    int id = atoi(argv[1]);
    const char *tempsensor_addr = argv[2];
    int max_wait_condvar = atoi(argv[3]);
    int max_wait_update = atoi(argv[4]);
    const char *shm_path = argv[5];
    off_t shm_offset = (off_t)atoi(argv[6]);

    const char *portString = strstr(tempsensor_addr, ":");
    int portNumber = atoi(portString + 1);

    struct timespec condWait;
    condWait.tv_sec = 0;
    condWait.tv_sec += max_wait_condvar / 1000000;
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
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&sensor_addr, '\0', sizeof(sensor_addr));
    sensor_addr.sin_family = AF_INET;
    sensor_addr.sin_port = htons(portNumber);
    sensor_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    /* bind server address to socket descriptor */
    if (bind(sockfd, (struct sockaddr *)&sensor_addr, sizeof(sensor_addr)) == -1)
    {
        perror("[-]bind error");
        return 1;
    }
    
    
    //fcntl(sockfd, F_SETFL, O_NONBLOCK);
    
    // Create addr_entry instance containing this sensor's details
    struct addr_entry thisSensor;
    if (inet_pton(AF_INET, "127.0.0.1", &(thisSensor.sensor_addr)) <= 0)
    {
        perror("Invalid address");
        return 1; // Handle the error appropriately
    }
    thisSensor.sensor_port = portNumber;

    // mutex lock for normal operation
    pthread_mutex_lock(&shared->mutex);
    float oldTemp = shared->temperature;
    int firstIteration = 1; // see if this is first iteration of for loop. 1 for true, 0 for false
    for (;;)
    {
        firstIteration = 0;
        int enteredifagain = 0;
        float currentTemp = shared->temperature;
        pthread_mutex_unlock(&shared->mutex);
        if (currentTemp != oldTemp || firstIteration == 1 || hasMaxWaitTimePassed(max_wait_update))
        {
            if (enteredifagain == 1) {
                printf("entered if again \n");
            }
            // construct a struct that contains sensor's id, temp and current time and address list of only this sensor

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

            // Add this sensor's details to the list
            datagram.address_list[0] = thisSensor;

            //  send datagram to each receiver
            for (int i = 7; i < argc; i++)
            {
                //printf("iteration of for loop no. %d\n", i);
                char *receiver_address_port = argv[i];
                char *receiverPortString = strstr(receiver_address_port, ":");
                int receiverPortNumber = atoi(receiverPortString + 1);
                //printf("iteration of for loop no. %d\n", i);
                receiver_addr.sin_family = AF_INET;
                receiver_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                receiver_addr.sin_port = htons(receiverPortNumber);
                //printf("iteration of for loop no. %d\n", i);

                if (sendto(sockfd, &datagram, sizeof(datagram), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) == -1)
                {
                    perror("sendto failed");
                    exit(1);
                }
                //printf("%d message sent line 193\nafter sending message iteration of for loop no. %d\n", id, i);
            }
            //printf("left for loop line 195\n");
        }
        enteredifagain =1 ;
        //int len = sizeof(client_addr);
        addr_size = sizeof(client_addr);
        //printf("int len line 197\n");
        int n = recvfrom(sockfd, &receiveBuffer, MAX_BUFFER_SIZE, MSG_DONTWAIT, (struct sockaddr *)&client_addr, &addr_size);
        while (n > 0)
        {
            printf("entered infinite while loop \n");
            printf(" n>0 line 196\n");
            int position;
            memcpy(&receivedDatagram, &receiveBuffer, sizeof(receivedDatagram));
            printf("memcpy good line 199");
            float receivedTemperature = receivedDatagram.temperature;
            printf(" got temperature %f line 201", receivedTemperature);
            struct timeval receivedTimeStamp;
            receivedTimeStamp = receivedDatagram.timestamp;
            printf(" received timestamp line 204 \n");
            struct addr_entry receivedEntries[50];
            for (int i = 0; i < 50; i++)
            {
                receivedEntries[i] = receivedDatagram.address_list[i];
            }
            printf("copied all entries line 209 \n");

            printf("trying to see if received entries is full line 211\n");
            if (&receivedEntries[50].sensor_addr.s_addr != 0 && &receivedEntries[50].sensor_port != 0)
            {
                for (int i = 0; i < 50; i++)
                {
                    if (&receivedEntries[i].sensor_addr.s_addr != 0 && &receivedEntries[i].sensor_port != 0)
                    {
                        receivedEntries[i] = thisSensor;
                        position = i + 1;
                        break;
                    }
                }
            }

            else
            {
                for (int i = 0; i < 49; i++)
                {
                    receivedEntries[i] = receivedEntries[i + 1];
                }
                receivedEntries[49] = thisSensor;
                position = 50;
            }

            // create new datagram
            struct datagram_format passMessageOn;
            printf(" creating a new datagram line 232");
            passMessageOn.temperature = receivedTemperature;
            passMessageOn.timestamp = receivedTimeStamp;
            passMessageOn.id = id;
            passMessageOn.address_count = position;
            for (int i = 0; i < 49; i++)
            {
                passMessageOn.address_list[i] = receivedEntries[i];
            }

            // pass it on
            for (int i = 7; i <= argc; i++)
            {
                const char *address_port = argv[i];
                const char *receiverPortString = strstr(address_port, ":");
                int receiverPortNumber = atoi(receiverPortString + 1);
                for (int j = 0; j < 49; j++)
                {
                    if (receivedEntries[j].sensor_port == receiverPortNumber)
                    {
                        receiver_addr.sin_family = AF_INET;
                        receiver_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                        receiver_addr.sin_port = htons(receiverPortNumber);

                     /*   if (inet_pton(AF_INET, argv[i], &receiver_addr.sin_addr) <= 0)
                        {
                            perror("Invalid address");
                            exit(EXIT_FAILURE);
                        }
*/

                        if (sendto(sockfd, &passMessageOn, sizeof(passMessageOn), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) == -1)
                        {
                            perror("sendto failed");
                            exit(1);
                        }
                        break;
                    }
                }
            }
            n = n -1;
        }
        pthread_mutex_lock(&shared->mutex);
        pthread_cond_timedwait(&shared->cond, &shared->mutex, &condWait);
    }

    close(sockfd);

    // general cleanup with error handling
    if (shm_unlink(shm_path) == -1)
    {
        perror("shm_unlink()");
        exit(1);
    }

    if (pthread_mutex_destroy(&shared->mutex) != 0)
    {
        perror("pthread_mutex_destroy()");
        exit(1);
    }

    if (pthread_cond_destroy(&shared->cond) != 0)
    {
        perror("pthread_cond_destroy");
        exit(1);
    }

    if (munmap(shm, shm_stat.st_size) == -1)
    {
        perror("munmap()");
    }

    close(shm_fd);

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