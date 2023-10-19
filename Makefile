CC=gcc
CFLAGS=-pthread -Wall
LDFLAGS=-pthread -lrt

all: cardreader door

cardreader: cardreader.o tcp_communication.o
	$(CC) $(CFLAGS) -o cardreader cardreader.o tcp_communication.o $(LDFLAGS)

cardreader.o: cardreader.c tcp_communication.h
	$(CC) $(CFLAGS) -c cardreader.c

tcp_communication.o: tcp_communication.c tcp_communication.h
	$(CC) $(CFLAGS) -c tcp_communication.c

door: door.o
	$(CC) $(CFLAGS) -o door door.o $(LDFLAGS)

door.o: door.c
	$(CC) $(CFLAGS) -c door.c

clean:
	rm -f cardreader door *.o