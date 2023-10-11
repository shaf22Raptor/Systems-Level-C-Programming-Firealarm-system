void configureServerAddress(struct sockaddr_in *serverAddr, const char *server_ip, int server_port);
int createSocket();
int establishConnection(int socket, struct sockaddr_in *serverAddr);
int sendData(int socket, const char *data);
int receiveData(int socket, char *buffer, int buffer_size);