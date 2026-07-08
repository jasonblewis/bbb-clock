CC = gcc
# gnu17 (not c17): the vendored TSL2561 lib uses POSIX usleep(), hidden under strict c17.
CFLAGS = -std=gnu17 -g -Wall -I./tsl2561
LIBS = -lm
LIBSRC = tsl2561/TSL2561.c

.PHONY: all clean
all: tsl2561-daemon client

# Daemon: server + vendored sensor lib. client.c has its own main() -> built separately.
tsl2561-daemon: tsl2561-daemon.c $(LIBSRC) tsl2561/TSL2561.h
	$(CC) $(CFLAGS) -o $@ tsl2561-daemon.c $(LIBSRC) $(LIBS)

client: client.c
	$(CC) $(CFLAGS) -o $@ client.c $(LIBS)

clean:
	-rm -f tsl2561-daemon client *.o
