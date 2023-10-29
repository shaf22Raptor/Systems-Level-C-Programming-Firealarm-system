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

// shared memory struct. This data will be shared with simulator.
typedef struct
{
    float temperature;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shm_sensor;

// Datagram format for each address entry that will be in datagram
struct addr_entry
{
    struct in_addr sensor_addr;
    in_port_t sensor_port;
};

// Datagram that will be sent by tempsensor with all necessary details
struct datagram_format
{
    char header[4]; // {'T', 'E', 'M', 'P'}
    struct timeval timestamp;
    float temperature;
    uint16_t id;
    uint8_t address_count;
    struct addr_entry address_list[50];
};

// used to search addr_entry array to see if a particular address is already in it
int search(struct addr_entry entries[], int PortNumber, int numberEntries);
void updateLastUpdateTime();
int hasMaxWaitTimePassed(int maxUpdateWait);

int main(int argc, char **argv)
{
    // Sees if enough command line arguments were supplied
    if (argc < 6)
    {
        fprintf(stderr, "usage: {id} {address:port} {max condvar wait (microseconds)} {max update wait (microseconds)} {shared memory path} {shared memory offset} {receiver address:port}...");
        exit(1);
    }

    // declare all addresses to be used in system
    struct sockaddr_in sensor_addr, receiver_addr, client_addr;

    // declare all types of datagrams to be sent
    struct datagram_format datagram, receivedDatagram, passMessageOn;
    socklen_t addr_size;

    // Configure buffer for receiving data
    char receiveBuffer[MAX_BUFFER_SIZE];

    // intialise parameters for system
    int id = atoi(argv[1]);
    const char *tempsensor_addr = argv[2];
    int max_wait_condvar = atoi(argv[3]);
    int max_wait_update = atoi(argv[4]);
    const char *shm_path = argv[5];
    off_t shm_offset = (off_t)atoi(argv[6]);

    // Isolate port number from address number
    const char *portString = strstr(tempsensor_addr, ":");
    int portNumber = atoi(portString + 1);

    // configure initial UDP packet
    struct timespec condWait;
    condWait.tv_sec = 0;
    condWait.tv_nsec = 0;
    condWait.tv_sec += max_wait_condvar / 1000000;
    condWait.tv_nsec += (max_wait_condvar % 1000000) * 1000;

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

    // mmap with error checking
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

    // configure connection for sensor to act as server for other sensors in system
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

    // Set socket to non-blocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    // Create addr_entry instance containing this sensor's details
    struct addr_entry thisSensor;
    if (inet_pton(AF_INET, "127.0.0.1", &(thisSensor.sensor_addr)) <= 0)
    {
        perror("Invalid address");
        return 1;
    }
    thisSensor.sensor_port = portNumber;

    // mutex lock for normal operation
    pthread_mutex_lock(&shared->mutex);
    float oldTemp = shared->temperature;
    float currentTemp;
    int firstIteration = 1; // see if this is first iteration of for loop. 1 for true, 0 for false
    for (;;)
    {
        // update temperature reading 
        currentTemp = shared->temperature;
        pthread_mutex_unlock(&shared->mutex);
        // send new datagram if temperature changes, if this is the first iteration of the for loop, or if max delay for info update has passed
        if (currentTemp != oldTemp || firstIteration == 1 || hasMaxWaitTimePassed(max_wait_update) == 1)
        {
            firstIteration = 0;
            oldTemp = currentTemp;
            // construct a struct that contains sensor's id, temp and current time and address list of only this sensor

            // construct header
            strcpy(datagram.header, "TEMP");

            // timestamp
            struct timeval timeStamp;
            gettimeofday(&timeStamp, NULL);

            // temparture
            datagram.temperature = currentTemp;
            datagram.timestamp = timeStamp;

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
                char *receiver_address_port = argv[i];
                char *receiverPortString = strstr(receiver_address_port, ":");
                int receiverPortNumber = atoi(receiverPortString + 1);

                receiver_addr.sin_family = AF_INET;
                receiver_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                receiver_addr.sin_port = htons(receiverPortNumber);

                if (sendto(sockfd, &datagram, sizeof(datagram), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) == -1)
                {
                    perror("sendto failed");
                    exit(1);
                }
                // update time since the datagram was last sent to receivers
                updateLastUpdateTime();
            }
        }
        // configure size of address for receiving
        addr_size = sizeof(client_addr);
        while (1)
        {
            // receive data from other devices in network
            int n = recvfrom(sockfd, &receiveBuffer, MAX_BUFFER_SIZE, MSG_DONTWAIT, (struct sockaddr *)&client_addr, &addr_size);
            // if no datagrams received, exit while loop
            if (n <= 0)
            {
                break;
            }

            // copy received data into a new datagram struct instance
            memcpy(&receivedDatagram, &receiveBuffer, sizeof(receivedDatagram));

            // copy header from received datagram
            char receivedHeader[4];
            strcpy(receivedHeader, receivedDatagram.header);

            // copy timestamp from received datagram
            struct timeval receivedTimeStamp;
            receivedTimeStamp = receivedDatagram.timestamp;

            // copy temperature from datagram
            float receivedTemperature = receivedDatagram.temperature;

            // copy ID from received datagram
            int receivedId = receivedDatagram.id;

            // copy addresses and address count into new datagram
            int received_address_count = receivedDatagram.address_count;
            struct addr_entry receivedEntries[50];

            // copy received entires into the new datagram that will be posted
            for (int i = 0; i < 50; i++)
            {
                receivedEntries[i] = receivedDatagram.address_list[i];
            }

            // Now add this sensor's details to the address list
            // see if this list has 50 entries already.
            if (receivedEntries[49].sensor_addr.s_addr == 0 && receivedEntries[49].sensor_port == 0)
            {
                // see which position this sensor should be placed in the list
                for (int i = 0; i < 50; i++)
                {
                    if (receivedEntries[i].sensor_addr.s_addr != 0 && receivedEntries[i].sensor_port != 0)
                    {
                        receivedEntries[i] = thisSensor;
                        received_address_count++;
                        break;
                    }
                }
            }

            // if the list already has 50 entries then:
            else
            {
                // shift every entry i - 1 places forward (first enrty will be replaced with second entry, 2nd with 3rd, etc)
                for (int i = 0; i < 50; i++)
                {
                    receivedEntries[i] = receivedEntries[i + 1];
                }
                // place this sensor's details in the last position of the address list
                receivedEntries[49] = thisSensor;
            }

            // create new datagram and populate with all relevant data
            struct datagram_format passMessageOn;
            strcpy(passMessageOn.header, receivedHeader);
            passMessageOn.timestamp = receivedTimeStamp;
            passMessageOn.temperature = receivedTemperature;
            passMessageOn.id = receivedId;
            passMessageOn.address_count = received_address_count + 1;

            // copy received addresses list to the list that will be posted to receivers
            for (int i = 0; i < 50; i++)
            {
                passMessageOn.address_list[i] = receivedEntries[i];
            }

            // check to see if the received address list already contains any of the receivers this sensor is supposed to send data to
            for (int i = 7; i < argc; i++)
            {
                const char *address_port = argv[i];
                const char *receiverPortString = strstr(address_port, ":");
                int receiverPortNumber = atoi(receiverPortString + 1);

                // use a search algorithm to find receiver addresses in the received address list
                if (search(receivedEntries, receiverPortNumber, 50) == 1)
                {
                    // if addresses not found, then add receiver data to the address list
                    receiver_addr.sin_family = AF_INET;
                    receiver_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                    receiver_addr.sin_port = htons(receiverPortNumber);

                    // send to receiver
                    if (sendto(sockfd, &passMessageOn, sizeof(passMessageOn), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) == -1)
                    {
                        perror("sendto failed");
                        exit(1);
                    }
                }
            }
            // clear message buffer
            receiveBuffer[n] = '\0';
            memset(receiveBuffer, 0, sizeof(receiveBuffer));
        }

        pthread_mutex_lock(&shared->mutex);
        // use timedwait to allow shared memory to be updated
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

// search function to see if address list contains a particular address
int search(struct addr_entry entries[], int PortNumber, int numberEntries)
{
    for (int i = 0; i < numberEntries; i++)
    {
        if (entries[i].sensor_port == PortNumber)
        {
            return -1;
        }
    }
    return 1;
}

// save the current time. This will primarily be used to see when a message was last sent by the tempsensor
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
    int timePassed_sec = (currentTime.tv_sec - lastUpdateTime.tv_sec) * 1000000;
    int timePassed_usec = currentTime.tv_usec - lastUpdateTime.tv_usec;
    int timePassed = timePassed_sec + timePassed_usec;
    // Compare with the maximum update wait time
    if (timePassed > maxUpdateWait)
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