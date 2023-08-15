CC = gcc

LDFLAGS= -pthread  -lpthread

all:client server
client : client.o
	$(CC) -o client client.o -lpthread -lm -D_GNU_SOURCE
client.o: client.c
	$(CC) -c client.c

server: server.o
	$(CC) -o server server.o -lpthread -lm -D_GNU_SOURCE

server.o: server.c
	$(CC) -c server.c

clean:
	rm -f  *.out *.o
