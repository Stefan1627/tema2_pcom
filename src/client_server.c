#include "../include/client_server.h"

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
	cleanup_client_subscriptions(c);
    close(c->fd);
    free(c);
}

int client_handle_data(client_t *c) {
	int ret = 0;
    // read whatever arrived
    ssize_t r = recv(c->fd,
                     c->read_buf + c->read_buf_len,
                     READ_BUF_SIZE - c->read_buf_len,
                     0);

	// diconnected or error
    if (r <= 0) return -1;

    c->read_buf_len += r;
    size_t off = 0;

    // parse as many full messages as possible
    while (c->read_buf_len - off >= sizeof(MsgHeader)) {
        MsgHeader hdr;
        memcpy(&hdr, c->read_buf + off, sizeof(hdr));
        uint16_t type = ntohs(hdr.type);
        uint32_t len  = ntohl(hdr.length);

		// not a full payload yet
        if (c->read_buf_len - off < sizeof(hdr) + len)
            break;

        char *payload = c->read_buf + off + sizeof(hdr);

        switch (type) {
        case MSG_SUBSCRIBE: {
			ret = trie_subscribe(c, payload);
			if (ret < 0) return -1;
			else send_message(c->fd, MSG_SUBSCRIBE_ACK, payload, len);
            break;
			}

        case MSG_UNSUBSCRIBE:
			ret = trie_unsubscribe(c, payload);
			if (ret < 0) return -1;
			else send_message(c->fd, MSG_UNSUBSCRIBE_ACK, payload, len);
            break;

        default:
            // ignore or error
            break;
        }

        off += sizeof(hdr) + len;
    }

    // compact any leftover bytes
    if (off > 0) {
        memmove(c->read_buf,
                c->read_buf + off,
                c->read_buf_len - off);
        c->read_buf_len -= off;
    }
    return 0;
}
