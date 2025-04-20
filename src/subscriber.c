#include "../include/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SUB_LEN 10
#define UNSUB_LEN 12
#define READ_BUF_SIZE 2048

int handle_received_data(int sockfd)
{
	unsigned char buf[READ_BUF_SIZE];
	ssize_t n = recv(sockfd, buf, READ_BUF_SIZE, 0);
	if (n == 0)
	{
		return 0;
	}
	if (n < 0)
	{
		perror("recv");
		return -1;
	}
	if (n < (ssize_t)sizeof(MsgHeader))
	{
		fprintf(stderr, "Short read: only %zd bytes (need %zu for header)\n",
				n, sizeof(MsgHeader));
		return -1;
	}

	// pull out header
	MsgHeader hdr;
	memcpy(&hdr, buf, sizeof(hdr));
	uint16_t type = ntohs(hdr.type);
	uint32_t length = ntohl(hdr.length);

	// make sure full payload arrived
	size_t total_needed = sizeof(hdr) + length;
	if (n < (ssize_t)total_needed)
	{
		fprintf(stderr,
				"Short read: got %zd bytes, need %zu (header + payload) hello\n",
				n, total_needed);
		return -1;
	}

	// point at payload in-place and NUL‑terminate for printing
	unsigned char *payload = buf + sizeof(hdr);
	// we know buf was at least total_needed bytes long
	payload[length] = '\0';

	switch (type)
	{
	case MSG_PUBLISH:
		printf(">> PUBLISH: %s\n", payload);
		break;
	case MSG_SUBSCRIBE_ACK:
		printf("Subscribed to topic %s\n", payload);
		break;
	case MSG_UNSUBSCRIBE_ACK:
		printf("Unsubscribed from topic %s\n", payload);
		break;
	default:
		printf(">> MSG_TYPE %u: %.*s\n",
			   type,
			   length,
			   payload);
		break;
	}
	
	return 1;
}

int main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);

	if (argc != 4)
	{
		fprintf(stderr, "Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n",
				argv[0]);
		exit(EXIT_FAILURE);
	}

	const char *client_id = argv[1];
	const char *server_ip = argv[2];
	int server_port = atoi(argv[3]);
	if (server_port <= 0)
	{
		fprintf(stderr, "Error: invalid port number '%s'\n", argv[3]);
		exit(EXIT_FAILURE);
	}

	// Create TCP socket
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
	{
		perror("socket");
		exit(EXIT_FAILURE);
	}

	// Build server address and connect
	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(server_port);
	serv_addr.sin_addr.s_addr = inet_addr(server_ip);

	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
	{
		printf("connection with the server failed...\n");
		exit(0);
	}

	// Send client ID (terminated by '\n')
	char buf[32];
	int len = snprintf(buf, sizeof(buf), "%s\n", client_id);
	if (len < 0 || len >= (int)sizeof(buf))
	{
		fprintf(stderr, "Error: client ID too long\n");
		close(sockfd);
		exit(EXIT_FAILURE);
	}
	if (send(sockfd, buf, len, 0) != len)
	{
		perror("send");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	// --- Now keep the connection open, watch for "exit" on stdin ---
	fd_set read_fds;
	int maxfd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;

	while (1)
	{
		FD_ZERO(&read_fds);
		FD_SET(STDIN_FILENO, &read_fds);
		FD_SET(sockfd, &read_fds);

		if (select(maxfd + 1, &read_fds, NULL, NULL, NULL) < 0)
		{
			perror("select");
			break;
		}

		// Handle user typing "exit"
		if (FD_ISSET(STDIN_FILENO, &read_fds))
		{
			char line[128];
			if (!fgets(line, sizeof(line), stdin))
			{
				break;
			}
			// strip newline
			line[strcspn(line, "\n")] = '\0';
			if (strcmp(line, "exit") == 0)
			{
				// clean shutdown
				break;
			}
			else if (strstr(line, "unsubscribe") != 0)
			{
				int ret = send_message(sockfd, MSG_UNSUBSCRIBE, line + UNSUB_LEN, strlen(line) - UNSUB_LEN);
				if (ret < 0)
				{
					perror("send_message");
					break;
				}
			}
			else if (strstr(line, "subscribe") != 0)
			{
				int ret = send_message(sockfd, MSG_SUBSCRIBE, line + SUB_LEN, strlen(line) - SUB_LEN);
				if (ret < 0)
				{
					perror("send_message");
					break;
				}
			}
		}

		// Handle data from server
		if (FD_ISSET(sockfd, &read_fds))
		{
			int ret = handle_received_data(sockfd);

			if (ret < 1)
				break;
		}
	}

	close(sockfd);
	return 0;
}
