TARGET = server
LIBS = -lm
CC = gcc
CFLAGS = -g -Wall
INCLUDES = -I./tsl2561

.PHONY: default all clean

default: $(TARGET)

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES)x -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	-rm -f *.o
	-rm -f $(TARGET)

tsl2561:
	$(MAKE) -C tsl2561

all: tsl2561 default
