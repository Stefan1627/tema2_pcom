#ifndef CLIENT_H
#define CLIENT_H

#include <stddef.h>
#include <stdint.h>

#define READ_BUF_SIZE 2048

typedef struct client {
    int             fd;             // socket
    char            id[16];         // client identifier
    uint8_t         read_buf[READ_BUF_SIZE];
    size_t          read_buf_len;   // how many bytes are in read_buf
    struct client  *next;
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
