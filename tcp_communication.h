void configureServerAddress(struct sockaddr_in serverAddr, const char server_ip);
int createSocket();
void establishConnection(int socket, struct sockaddr_in serverAddr, int portNumber);
void sendData(int socket, char *data);
int receiveData(int socket, char *buffer);