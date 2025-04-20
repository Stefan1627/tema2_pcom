#ifndef CLIENT_H
#define CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include "topic_trie.h"
#include "protocol.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>

#define READ_BUF_SIZE 2048

typedef struct sub_ref sub_ref_t;

typedef struct client {
    int             fd;             // socket
    char            id[16];         // client identifier
    char            read_buf[READ_BUF_SIZE];
    size_t          read_buf_len;   // how many bytes are in read_buf
    struct client  *next;

	sub_ref_t    *subscriptions;
} client_t;

// Allocate, initialize (incl. TCP_NODELAY), return NULL on error
client_t *client_create(int fd, const char *id);

// Tear down a client (close + free)
void client_destroy(client_t *c);

// Read() from c->fd into its buffer, parse as many messages
// (SUBSCRIBE/UNSUBSCRIBE), compact leftovers.
// Returns -1 on disconnect/error, 0 otherwise.
int client_handle_data(client_t *c);

#endif // CLIENT_H
