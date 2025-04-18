// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct client
{
	int fd;
	char id[16];
	struct client *next;
} client_t;

int main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <PORT>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	int port = atoi(argv[1]);

	// --- Create & bind UDP socket ---
	int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (udp_fd < 0)
	{
		perror("socket udp");
		exit(EXIT_FAILURE);
	}

	// --- Create, bind & listen on TCP socket ---
	int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_fd < 0)
	{
		perror("socket tcp");
		exit(EXIT_FAILURE);
	}

	// allow immediate reuse of the port
	int one = 1;
	if (setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0 ||
		setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
	{
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

	// common bind address
	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(udp_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("bind udp");
		exit(EXIT_FAILURE);
	}
	if (bind(tcp_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("bind tcp");
		exit(EXIT_FAILURE);
	}

	if (listen(tcp_fd, SOMAXCONN) < 0)
	{
		perror("listen");
		exit(EXIT_FAILURE);
	}

	// --- Prepare client list ---
	client_t *clients = NULL;
	int next_id = 1;
	int exit_flag = 0;

	printf("Server listening on port %d (TCP+UDP)\n", port);

	// --- Main select() loop ---
	while (!exit_flag)
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);

		// always watch stdin for "exit"
		FD_SET(STDIN_FILENO, &read_fds);
		// watch our two sockets
		FD_SET(udp_fd, &read_fds);
		FD_SET(tcp_fd, &read_fds);

		int maxfd = udp_fd > tcp_fd ? udp_fd : tcp_fd;

		// watch every connected client
		for (client_t *c = clients; c; c = c->next)
		{
			FD_SET(c->fd, &read_fds);
			if (c->fd > maxfd)
				maxfd = c->fd;
		}

		int ready = select(maxfd + 1, &read_fds, NULL, NULL, NULL);
		if (ready < 0)
		{
			perror("select");
			break;
		}

		// Did user type something?
		if (FD_ISSET(STDIN_FILENO, &read_fds))
		{
			char line[32];
			if (fgets(line, sizeof(line), stdin) &&
				strcmp(line, "exit\n") == 0)
			{
				exit_flag = 1;
			}
		}

		// New TCP subscriber?
		if (FD_ISSET(tcp_fd, &read_fds))
		{
			struct sockaddr_in cli_addr;
			socklen_t cli_len = sizeof(cli_addr);
			int newfd = accept(tcp_fd,
							   (struct sockaddr *)&cli_addr,
							   &cli_len);
			if (newfd < 0)
			{
				perror("accept");
				continue;
			}

			// --- Read the client ID they just sent ---
			char id_buf[16];
			ssize_t id_len = recv(newfd, id_buf, sizeof(id_buf) - 1, 0);
			if (id_len <= 0)
			{
				// client closed immediately or error
				close(newfd);
				continue;
			}
			id_buf[id_len] = '\0';
			// strip trailing newline or space
			id_buf[strcspn(id_buf, "\r\n")] = '\0';

			// --- Check for duplicate ID ---
			client_t *iter = clients;
			bool duplicate = false;
			while (iter)
			{
				if (strcmp(iter->id, id_buf) == 0)
				{
					duplicate = true;
					break;
				}
				iter = iter->next;
			}
			if (duplicate)
			{
				printf("Client %s already connected.", id_buf);
				close(newfd);
				continue;
			}

			// --- Allocate and link in the new client ---
			client_t *nc = malloc(sizeof(*nc));
			if (!nc)
			{
				perror("malloc");
				close(newfd);
				continue;
			}
			nc->fd = newfd;
			strncpy(nc->id, id_buf, sizeof(nc->id));
			nc->id[sizeof(nc->id) - 1] = '\0';
			nc->next = clients;
			clients = nc;

			printf("New client %s connected from %s:%d.\n",
				   nc->id,
				   inet_ntoa(cli_addr.sin_addr),
				   ntohs(cli_addr.sin_port));
		}

		// Incoming UDP packet?
		if (FD_ISSET(udp_fd, &read_fds))
		{
			char buf[1500];
			struct sockaddr_in src;
			socklen_t slen = sizeof(src);
			ssize_t n = recvfrom(udp_fd, buf, sizeof(buf), 0,
								 (struct sockaddr *)&src, &slen);
			if (n > 0)
			{
				printf("Received UDP packet (%zd bytes) from %s:%d\n",
					   n,
					   inet_ntoa(src.sin_addr),
					   ntohs(src.sin_port));
			}
			else if (n < 0)
			{
				perror("recvfrom");
			}
		}

		// Data or disconnect from any TCP client
		client_t *prev = NULL;
		client_t *c = clients;
		while (c)
		{
			if (FD_ISSET(c->fd, &read_fds))
			{
				char junk[128];
				ssize_t r = recv(c->fd, junk, sizeof(junk) - 1, 0);
				if (r <= 0)
				{
					// client closed
					printf("Client %s disconnected.\n", c->id);
					close(c->fd);
					client_t *dead = c;
					c = c->next;
					if (prev)
						prev->next = c;
					else
						clients = c;
					free(dead);
					continue;
				}
				// otherwise: we’d parse subscribe/unsubscribe here
			}
			prev = c;
			c = c->next;
		}
	}

	// --- Clean up ---
	for (client_t *c = clients; c;)
	{
		close(c->fd);
		client_t *tmp = c;
		c = c->next;
		free(tmp);
	}
	close(tcp_fd);
	close(udp_fd);
	return 0;
}
