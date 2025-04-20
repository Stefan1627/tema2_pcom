CC      := gcc
CFLAGS  := -Wall -Wextra -O2

SRCDIR  := src
SRCS    := $(SRCDIR)/protocol.c \
		   $(SRCDIR)/topic_trie.c \
           $(SRCDIR)/client_server.c \
           $(SRCDIR)/server.c
OBJS    := $(SRCS:.c=.o)
OBJS2   := src/subscriber.c src/protocol.o

TARGETS := server subscriber

all: $(TARGETS)

server: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

subscriber: $(OBJS2)
	$(CC) $(CFLAGS) -o $@ $(OBJS2)

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGETS)
