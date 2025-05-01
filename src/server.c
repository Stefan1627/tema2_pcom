// 324CC Stefan CALMAC
#include "../include/client_server.h"
#include "../include/protocol.h"
#include "../include/topic_trie.h"

#include <poll.h>

#define MAX_UDP_PAYLOAD 1500

ssize_t build_packet(struct sockaddr_in *src,
					 char *buf,
					 ssize_t payload_len)
{
	char header[INET_ADDRSTRLEN + 1 + 6 + 2];
	int header_len = snprintf(
		header, sizeof(header),
		"%s %u ",
		inet_ntoa(src->sin_addr),
		ntohs(src->sin_port));
	if (header_len < 0 || header_len >= (int)sizeof(header)) {
		return payload_len;
	}
	memmove(buf + header_len, buf, payload_len);
	memcpy(buf, header, header_len);
	return header_len + payload_len;
}

char *extract_topic(char *msg)
{
	if (!msg)
		return NULL;

	char *topic = malloc(MAX_TOPIC_LEN + 1);
	if (!topic)
		return NULL;

	int i;

	for (i = 0; i < MAX_TOPIC_LEN && msg[i] != '\0'; i++) {
		topic[i] = msg[i];
	}
	topic[i] = '\0';

	return topic;
}

void run_server(int port)
{
	int one = 1;

	// Setup sockets
	// UDP socket
	int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (udp_fd < 0) {
		perror("socket udp");
		exit(1);
	}

	if (setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
		perror("setsockopt UDP");
		exit(1);
	}

	// TCP socket
	int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_fd < 0) {
		perror("socket tcp");
		exit(1);
	}

	if (setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
		perror("setsockopt TCP");
		exit(1);
	}

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr.s_addr = INADDR_ANY};
	if (bind(udp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind UDP");
		exit(1);
	}

	if (bind(tcp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind TCP");
		exit(1);
	}

	if (listen(tcp_fd, SOMAXCONN) < 0) {
		perror("listen TCP");
		exit(1);
	}

	// Client lists: active and inactive (to preserve subscriptions)
	client_t *clients = NULL;
	client_t *inactive_clients = NULL;
	int client_count = 0;
	int exit_flag = 0;

	// Trie init
	topic_node_t *root = node_create(NULL, CHILD_NAME, NULL);
	if (root == NULL) {
		perror("Invalid root");
		exit(1);
	}

	while (!exit_flag) {
		// build poll fds
		int nfds = 3 + client_count;
		struct pollfd *pfds = malloc(nfds * sizeof(*pfds));
		if (!pfds) {
			perror("malloc pfds");
			break;
		}

		// 0 = stdin, 1 = udp, 2 = tcp accept
		pfds[0] = (struct pollfd){.fd = STDIN_FILENO, .events = POLLIN};
		pfds[1] = (struct pollfd){.fd = udp_fd, .events = POLLIN};
		pfds[2] = (struct pollfd){.fd = tcp_fd, .events = POLLIN};

		int idx = 3;
		for (client_t *c = clients; c; c = c->next) {
			pfds[idx++] = (struct pollfd){.fd = c->fd, .events = POLLIN};
		}

		if (poll(pfds, nfds, -1) < 0) {
			perror("poll");
			free(pfds);
			break;
		}

		idx = 0;
		// — exit on stdin —
		if (pfds[idx++].revents & POLLIN) {
			char buf[32];
			if (fgets(buf, sizeof(buf), stdin) &&
				strcmp(buf, "exit\n") == 0)
				exit_flag = 1;
		}

		// — incoming UDP? —
		if (pfds[idx].revents & POLLIN) {
			char buf[MAX_UDP_PAYLOAD];
			struct sockaddr_in src;
			socklen_t slen = sizeof(src);
			ssize_t n = recvfrom(udp_fd, buf, sizeof(buf), 0,
								 (struct sockaddr *)&src, &slen);
			if (n > 0)
			{
				char *topic = extract_topic(buf);
				if (strlen(topic) > MAX_TOPIC_LEN) {
					perror("Topic too long");
					exit(1);
				}

				n = build_packet(&src, buf, n);
				if (n > 0)
					trie_publish(root, topic, buf, n);
				free(topic);
			}
		}
		idx++;

		// — new TCP connection? —
		if (pfds[idx].revents & POLLIN) {
			struct sockaddr_in cli;
			socklen_t clilen = sizeof(cli);
			int newfd = accept(tcp_fd,
							   (struct sockaddr *)&cli,
							   &clilen);
			if (newfd >= 0) {
				char id[16];
				ssize_t len = recv(newfd, id, sizeof(id) - 1, 0);
				if (len > 0) {
					id[len] = '\0';
					id[strcspn(id, "\r\n")] = '\0';

					// check if already active
					bool is_active = false;
					for (client_t *it = clients; it; it = it->next) {
						if (strcmp(it->id, id) == 0) {
							is_active = true;
							break;
						}
					}
					if (is_active) {
						printf("Client %s already connected.\n", id);
						close(newfd);
					} else {
						// check inactive list for reconnection
						client_t *prev_in = NULL, *it = inactive_clients;
						while (it) {
							if (strcmp(it->id, id) == 0)
								break;
							prev_in = it;
							it = it->next;
						}

						if (it) {
							// reconnect existing client
							client_t *reconn = it;
							// unlink from inactive
							if (prev_in)
								prev_in->next = reconn->next;
							else
								inactive_clients = reconn->next;

							reconn->fd = newfd;
							reconn->read_buf_len = 0;
							reconn->next = clients;
							clients = reconn;
							client_count++;

							printf("New client %s connected from %s:%d.\n",
								   reconn->id,
								   inet_ntoa(cli.sin_addr),
								   ntohs(cli.sin_port));
						} else {
							// brand‑new client
							client_t *nc = client_create(newfd, id);
							if (nc) {
								nc->next = clients;
								clients = nc;
								client_count++;
								printf("New client %s connected from %s:%d.\n",
									   nc->id,
									   inet_ntoa(cli.sin_addr),
									   ntohs(cli.sin_port));
							} else {
								close(newfd);
							}
						}
					}
				} else {
					close(newfd);
				}
			}
		}
		idx++;

		// — handle TCP client data / disconnect —
		client_t *prev = NULL, *cur = clients;
		while (cur) {
			if (pfds[idx].revents & (POLLIN | POLLHUP | POLLERR)) {
				if (client_handle_data(root, cur) < 0) {
					// client disconnected: keep subscriptions
					// or error in connection
					printf("Client %s disconnected.\n", cur->id);
					client_count--;

					client_t *dead = cur;
					cur = cur->next;
					if (prev)
						prev->next = cur;
					else
						clients = cur;

					// move to inactive list
					close(dead->fd);
					dead->fd = -1;
					dead->next = inactive_clients;
					inactive_clients = dead;

					idx++;
					continue;
				}
			}
			prev = cur;
			cur = cur->next;
			idx++;
		}
		free(pfds);
	}

	// — final cleanup —
	for (client_t *c = clients; c;) {
		client_t *tmp = c;
		c = c->next;
		client_destroy(root, tmp);
	}
	for (client_t *c = inactive_clients; c;) {
		client_t *tmp = c;
		c = c->next;
		client_destroy(root, tmp);
	}
	close(tcp_fd);
	close(udp_fd);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <port>\n", argv[0]);
		return 1;
	}
	run_server(atoi(argv[1]));
	return 0;
}
