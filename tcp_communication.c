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
        perror("\nsocket()\n");
    }

    return sockfd;
}

/*void configureServerAddressForClient(struct sockaddr_in *addr, const char *server_ip, int *server_port) {
    memset(&addr, 0, sizeof(addr));
    if (inet_pton(AF_INET, "127.0.0.1", (struct sockaddr *)&addr.sin_addr) != 1) {
        perror("inet_pton()");
        return 1;
    }
    addr->sin_addr.s_addr = htonl(INADDR_ANY);
    addr->sin_port = htons(server_port);
}
*/
int establishConnection(int socket, struct sockaddr_in serverAddr, char *programName) {
    int connection_status = connect(socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (connection_status == -1) {
        printf("Error: Connection to the server failed for %s\n", programName);
    }
    return connection_status;
}

int sendData(int socket, char *data) {
    // Send data
    int sent = send(socket, data, strlen(data), 0);
    if (sent == -1) {
        printf("Error: Failed to send data\n");
    }

    return sent;
}

int receiveData(int socket, char *buffer) {
    // Receive data
    int dataReceived = recv(socket, buffer, sizeof(buffer), 0);
    if (dataReceived == -1) {
        printf("Error: receive data failed");
    }
    
    return dataReceived;
}