CC      := gcc
CFLAGS  := -Wall -Wextra -O2

SRCDIR  := src
SRCS    := $(SRCDIR)/protocol.c \
           $(SRCDIR)/client_server.c \
           $(SRCDIR)/server.c
OBJS    := $(SRCS:.c=.o)

TARGETS := server subscriber

all: $(TARGETS)

server: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

subscriber: src/subscriber.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGETS)
