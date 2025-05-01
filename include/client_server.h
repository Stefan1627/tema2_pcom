#ifndef CLIENT_H
#define CLIENT_H

#include "topic_trie.h"
#include "protocol.h"

#define READ_BUF_SIZE 2048

typedef struct topic_node topic_node_t;
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
void client_destroy(topic_node_t *root, client_t *c);

// Read() from c->fd into its buffer, parse as many messages
// (SUBSCRIBE/UNSUBSCRIBE), compact leftovers.
// Returns -1 on disconnect/error, 0 otherwise.
int client_handle_data(topic_node_t *root, client_t *c);

#endif // CLIENT_H
