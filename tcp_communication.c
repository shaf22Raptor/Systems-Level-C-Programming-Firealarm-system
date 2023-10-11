// tcp_operations.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <tcp_communication.h>

void configureServerAddress(struct sockaddr_in *serverAddr, const char *server_ip, int server_port) {
    serverAddr->sin_family = AF_INET;
    serverAddr->sin_port = htons(server_port);
    serverAddr->sin_addr.s_addr = inet_addr(server_ip);
}

int createSocket() {
    // Create a socket, set up the server address, and connect
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);   // Create socket for client and corresponding error handling
    if (sockfd == -1) { 
        return 1;
    }

    return sockfd;
}

int establishConnection(int socket, struct sockaddr_in *serverAddr) {
    int connection_status = connect(socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (connection_status == -1) {
        return 1;
    }
    return connection_status;
}

int sendData(int socket, const char *data) {
    // Send data
    send(socket, &data, strlen(&data), 0);
}

int receiveData(int socket, char *buffer, int buffer_size) {
    // Receive data
    // ...
}