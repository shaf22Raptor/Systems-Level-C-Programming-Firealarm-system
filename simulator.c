#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define MAX_SHARED_MEM_SIZE 54592

// Define constants for maximum components
#define MAX_CARD_READERS 40
#define MAX_DOORS 20
#define MAX_TEMP_SENSORS 20
#define MAX_CALL_POINTS 20
#define MAX_CAMERAS 20
#define MAX_DEST_SELECT 20
#define MAX_ELEVATORS 10

// Define constants for status values
#define CARD_READER_STATUS_LEN 10
#define DOOR_STATUS_LEN 10
#define CALL_POINT_STATUS_LEN 10
#define TEMP_SENSOR_STATUS_LEN 10
#define ELEVATOR_STATUS_LEN 10
#define DEST_SELECT_STATUS_LEN 10
#define CAMERA_STATUS_LEN 10

// Define the structure for shared memory
typedef struct {
    char security_alarm[10];

    // Fire alarm unit struct
    char alarm[10];

    // Card reader controller struct
    struct {
        char scanned[MAX_CARD_READERS][100];
        char response[MAX_CARD_READERS][100];
        pthread_mutex_t mutex;
        pthread_cond_t scanned_cond;
    } card_reader;

    // Door controller struct
    struct {
        char status[MAX_DOORS][DOOR_STATUS_LEN];
        pthread_mutex_t mutex;
        pthread_cond_t cond_start;
        pthread_cond_t cond_end;
    } door;

    // Fire alarm call-point controller struct
    struct {
        char status[MAX_CALL_POINTS][CALL_POINT_STATUS_LEN];
        pthread_mutex_t mutex;
        pthread_cond_t cond;
    } call_point;

    // Temperature sensor controller struct
    struct {
        float temperature[MAX_TEMP_SENSORS];
        pthread_mutex_t mutex;
        pthread_cond_t cond;
    } temp_sensor;

} SharedMemory;

// Initialize the shared memory space and components
void initializeSharedMemory() {
    key_t key = ftok("shmfile",65); // Generate a key for shared memory

    // Create shared memory segment
    int shmid = shmget(key, MAX_SHARED_MEM_SIZE, 0666|IPC_CREAT);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }

    // Attach to the shared memory segment
    SharedMemory *sharedMemory = (SharedMemory*) shmat(shmid, (void*)0, 0);
    if (sharedMemory == (void *)-1) {
        perror("shmat");
        exit(1);
    }

    // Initialize shared memory values for each component as specified in the specifications

    // Initialize overseer struct
    memset(sharedMemory->security_alarm, 0, sizeof(sharedMemory->security_alarm));

    // Initialize fire alarm unit struct
    memset(sharedMemory->alarm, 0, sizeof(sharedMemory->alarm));

    // Initialize card reader controller struct
    for (int i = 0; i < MAX_CARD_READERS; i++) {
        memset(sharedMemory->card_reader.scanned[i], 0, sizeof(sharedMemory->card_reader.scanned[i]));
        memset(sharedMemory->card_reader.response[i], 0, sizeof(sharedMemory->card_reader.response[i]));
    }
    // Initialize mutex and condition variables for the card reader
    // ...

    // Initialize other components' shared memory structures in a similar manner

    // Detach from shared memory
    shmdt(sharedMemory);
}

// Function to spawn processes
void spawnProcesses() {
    pid_t pid;
    // Spawning overseer process
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    } else if (pid == 0) {
        // Child process
        execl("path_to_overseer_executable", "overseer", NULL);
        // If execl fails
        perror("execl");
        exit(1);
    } else {
        // Parent process
        // Wait for some time (250 milliseconds) before spawning other processes
        usleep(250000);

        // Spawning other processes
        // ...
    }
}

// Simulate component behaviors based on events
void simulateComponents() {
    // Implement the simulation logic for each component based on the provided specifications
    // ...
}

// Read and interpret the scenario file
void readScenarioFile(const char* scenarioFile) {
    FILE* file = fopen(scenarioFile, "r");
    if (file == NULL) {
        perror("fopen");
        exit(1);
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Process each line of the scenario file
        // ...
    }

    fclose(file);
}

// Terminate the simulation
void terminateSimulation() {
    // Implement the termination logic to kill all other processes
    // Kill all other processes spawned during the simulation
    // ...

    // Cleanup shared memory segments
    // ...

    // Clean up mutexes, threads, and any other shared resources
    // ...

    // Exit the simulation
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <scenario_file>\n", argv[0]);
        return 1;
    }

    char* scenarioFile = argv[1];

    // Initialize shared memory
    initializeSharedMemory();

    // Spawn processes
    spawnProcesses();

    // Read and interpret the scenario file
    readScenarioFile(scenarioFile);

    // Terminate the simulation
    terminateSimulation();

    return 0;
}