# Makefile for server.c and subscriber.c

# Compiler and flags
CC      := gcc
CFLAGS  := -Wall -Wextra -O2

# Targets
TARGETS := server subscriber

# Default target: build both binaries
all: $(TARGETS)

# Rule to build server
server: server.c
	$(CC) $(CFLAGS) -o $@ $<

# Rule to build subscriber
subscriber: subscriber.c
	$(CC) $(CFLAGS) -o $@ $<

# Clean up generated files
.PHONY: clean
clean:
	rm -f $(TARGETS)
