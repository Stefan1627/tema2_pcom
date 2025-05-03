// 324CC Stefan CALMAC
#include "../include/client_server.h"

// Create + disable Nagle
client_t *client_create(int fd, const char *id)
{
	client_t *c = malloc(sizeof(*c));
	if (!c)
		return NULL;

	c->fd = fd;
	strncpy(c->id, id, sizeof(c->id) - 1);
	c->id[sizeof(c->id) - 1] = '\0';
	c->read_buf_len = 0;
	c->next = NULL;

	int flag = 1;

	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0)
		return NULL;

	return c;
}

void client_destroy(topic_node_t *root, client_t *c)
{
	cleanup_client_subscriptions(root, c);
	close(c->fd);
	free(c);
}

int client_handle_data(topic_node_t *root, client_t *c)
{
	const size_t HDR_SIZE = sizeof(uint16_t) + sizeof(uint32_t);

	// Read from socket
	ssize_t r = recv(c->fd,
					 c->read_buf + c->read_buf_len,
					 READ_BUF_SIZE - c->read_buf_len,
					 0);
	if (r <= 0) {
		// disconnected or error
		return -1;
	}
	c->read_buf_len += r;

	size_t off = 0;
	// Loop as long as we have at least a header’s worth
	while (c->read_buf_len - off >= HDR_SIZE) {
		// Pull out raw header fields
		uint16_t type_net;
		uint32_t len_net;
		memcpy(&type_net,
			   c->read_buf + off,
			   sizeof(type_net));
		memcpy(&len_net,
			   c->read_buf + off + sizeof(type_net),
			   sizeof(len_net));

		uint16_t type = ntohs(type_net);
		uint32_t len = ntohl(len_net);

		// Sanity‑check length
		if (len > READ_BUF_SIZE - HDR_SIZE)
		{
			fprintf(stderr,
					"client_handle_data: bogus length %u, dropping client\n",
					len);
			return -1;
		}

		// Wait until full payload arrives
		if (c->read_buf_len - off < HDR_SIZE + len)
			break;

		char *payload = c->read_buf + off + HDR_SIZE;
		payload[len] = '\0';
		switch (type) {
		case MSG_SUBSCRIBE:
			if (trie_subscribe(root, c, payload) < 0)
				return -1;
			if (send_message(c->fd, MSG_SUBSCRIBE_ACK, payload, len) < 0)
				return -1;
			break;

		case MSG_UNSUBSCRIBE:
			if (trie_unsubscribe(root, c, payload) < 0)
				return -1;
			if (send_message(c->fd, MSG_UNSUBSCRIBE_ACK, payload, len) < 0)
				return -1;
			break;

		default:
			// ignore unknown types
			break;
		}

		// advance past this message
		off += HDR_SIZE + len;
	}

	// Compact any leftover bytes
	if (off > 0) {
		memmove(c->read_buf,
				c->read_buf + off,
				c->read_buf_len - off);
		c->read_buf_len -= off;
	}

	return 0;
}
