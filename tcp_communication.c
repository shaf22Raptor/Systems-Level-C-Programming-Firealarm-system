#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int createSocket() {
    // Create a socket, set up the server address, and connect
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);   // Create socket for client and corresponding error handling
    if (sockfd == -1) { 
        perror("socket()");
        return 1;
    }
    return sockfd;
}

void configureServerAddressForClient(struct sockaddr_in addr, const char server_ip[]) {
    memset(&addr, 0, sizeof(addr));
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) != 1) {
        perror("inet_pton()");
        return 1;
    }
}

void establishConnection(int socket, struct sockaddr_in serverAddr, int portNumber) {
    
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(portNumber);
    
    int connection_status = connect(socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (connection_status == -1) {
        perror("connect()");
        return 1;
    }
}

void sendData(int socket, char *data) {
    // Send data
    int sent = send(socket, data, strlen(data), 0);
    if (sent == -1) {
        perror("send()");
        return 1;
    }
}

int receiveData(int socket, char *buffer) {
    // Receive data
    int dataReceived = recv(socket, buffer, sizeof(buffer), 0);
    if (dataReceived == -1) {
        perror("recv()");
        return 1;
    }
    return dataReceived;
}