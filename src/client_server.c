#include "../include/client_server.h"
#include "../include/protocol.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

// Create + disable Nagle
client_t *client_create(int fd, const char *id) {
    client_t *c = malloc(sizeof(*c));
    if (!c) return NULL;
    c->fd = fd;
    strncpy(c->id, id, sizeof(c->id)-1);
    c->id[sizeof(c->id)-1] = '\0';
    c->read_buf_len = 0;
    c->next = NULL;

    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    return c;
}

void client_destroy(client_t *c) {
    close(c->fd);
    free(c);
}

int client_handle_data(client_t *c) {
    // 1) read whatever arrived
    ssize_t r = recv(c->fd,
                     c->read_buf + c->read_buf_len,
                     READ_BUF_SIZE - c->read_buf_len,
                     0);
    if (r <= 0) return -1;   // disconnected or error

    c->read_buf_len += r;
    size_t off = 0;

    // 2) parse as many full messages as possible
    while (c->read_buf_len - off >= sizeof(MsgHeader)) {
        MsgHeader hdr;
        memcpy(&hdr, c->read_buf + off, sizeof(hdr));
        uint16_t type = ntohs(hdr.type);
        uint32_t len  = ntohl(hdr.length);

        if (c->read_buf_len - off < sizeof(hdr) + len)
            break;  // not a full payload yet

        uint8_t *payload = c->read_buf + off + sizeof(hdr);

        switch (type) {
        case MSG_SUBSCRIBE:
            // TODO: record subscription in client state
            printf("Client %s SUBSCRIBE %.*s\n",
                   c->id, len, (char*)payload);
            break;

        case MSG_UNSUBSCRIBE:
            // TODO: remove subscription
            printf("Client %s UNSUBSCRIBE %.*s\n",
                   c->id, len, (char*)payload);
            break;

        default:
            // ignore or error
            break;
        }

        off += sizeof(hdr) + len;
    }

    // 3) compact any leftover bytes
    if (off > 0) {
        memmove(c->read_buf,
                c->read_buf + off,
                c->read_buf_len - off);
        c->read_buf_len -= off;
    }
    return 0;
}
