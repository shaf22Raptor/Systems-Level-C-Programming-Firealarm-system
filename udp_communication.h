void configureServerAddress(struct sockaddr_in *serverAddr, const char *server_ip, int server_port);
int createSocket();
int sendData(int socket, const char *data, const char *recipientAddress);
int receiveData(int socket, char *buffer, const char *senderAddress);