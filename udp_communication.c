#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <udp_communication.h>

void configureServerAddress(struct sockaddr_in *recipientAddr, const char *recipient_addr_no, int recipient_port) {
    recipientAddr->sin_family = AF_INET;
    recipientAddr->sin_port = htons(recipient_port);
    recipientAddr->sin_addr.s_addr = inet_addr(recipient_addr_no);
}

int createSocket() {
    // Create a socket, set up the server address, and connect
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);   // Create socket for client and corresponding error handling
    if (sockfd == -1) { 
        perror("\nsocket()\n");
        return 1;
    }

    return sockfd;
}

int sendData(int socket, const char *data, const char *recipientAddress) {
    // Send data
    int sent = sendto(socket, &data, strlen(&data), 0, (struct sockaddr *)&recipientAddress, sizeof(&recipientAddress));
    if (sent == -1) {
        printf("Error: Failed to send data\n");
        return 1;
    }

    return sent;
}

int receiveData(int socket, char *buffer, const char *senderAddress) {
    // Receive data
    int dataReceived = recvfrom(socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&senderAddress, sizeof(&senderAddress));
    if (dataReceived == -1) {
        printf("Error: receive data failed");
        return 1;
    }
    
    return dataReceived;
}