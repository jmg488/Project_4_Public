CC = gcc
CFLAGS = -g -Wall -std=c99 -fsanitize=address,undefined

all: nimd_concurrent rawc testc

# Concurrent game server with extra credit (main submission)
nimd_concurrent: nimd_concurrent.o network.o
	$(CC) $(CFLAGS) -o $@ $^

# Raw client for testing
rawc: rawc.o pbuf.o network.o
	$(CC) $(CFLAGS) -o $@ $^

# Test client (easier to use)
testc: testc.o network.o
	$(CC) $(CFLAGS) -o $@ $^

nimd_concurrent.o: nimd_concurrent.c network.h
	$(CC) $(CFLAGS) -c nimd_concurrent.c

network.o: network.c network.h
	$(CC) $(CFLAGS) -c network.c

pbuf.o: pbuf.c pbuf.h
	$(CC) $(CFLAGS) -c pbuf.c

rawc.o: rawc.c network.h pbuf.h
	$(CC) $(CFLAGS) -c rawc.c

testc.o: testc.c network.h
	$(CC) $(CFLAGS) -c testc.c

clean:
	rm -f nimd_concurrent rawc testc *.o

.PHONY: all clean
