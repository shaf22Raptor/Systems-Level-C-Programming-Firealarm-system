CC=gcc
CFLAGS=-pthread -Wall
LDFLAGS=-pthread -lrt

all: cardreader door callpoint firealarm 

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

firealarm: firealarm.o
	$(CC) $(CFLAGS) -o firealarm firealarm.o $(LDFLAGS)

firealarm.o: firealarm.c
	$(CC) $(CFLAGS) -c firealarm.c	

callpoint: callpoint.o udp_communication.o
	$(CC) $(CFLAGS) -o callpoint callpoint.o udp_communication.o $(LDFLAGS)

callpoint.o: callpoint.c udp_communication.h
	$(CC) $(CFLAGS) -c callpoint.c

udp_communication.o: udp_communication.c udp_communication.h
	$(CC) $(CFLAGS) -c udp_communication.c

clean:
	rm -f cardreader door callpoint firealarm*.o