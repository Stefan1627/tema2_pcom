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
#define MAX_TOPIC_LEN 50

// Read exactly `len` bytes (or return 0/EOS or -1 on error)
static ssize_t recv_all(int sockfd, void *buf, size_t len)
{
	size_t total = 0;
	char *p = buf;
	while (total < len)
	{
		ssize_t r = recv(sockfd, p + total, len - total, 0);
		if (r < 0)
			return -1; // error
		if (r == 0)
			return 0; // peer closed
		total += r;
	}
	return total;
}

void process_payload(char *payload, size_t len)
{
	if (len < 1)
	{
		fprintf(stderr, "Empty payload\n");
		return;
	}
	uint8_t type = payload[0];
	const char *p = payload + 1;
	size_t remain = len - 1;

	switch (type)
	{
	case 0:
	{ // INT
		if (remain < 5)
		{
			fprintf(stderr, "Payload too short for INT\n");
			return;
		}
		uint8_t sign = p[0];
		uint32_t netv;
		memcpy(&netv, p + 1, 4);
		uint32_t v = ntohl(netv);
		if (sign)
			v = -v;
		printf("INT - %d\n", v);
		break;
	}
	case 1:
	{ // SHORT_REAL
		if (remain < 2)
		{
			fprintf(stderr, "Payload too short for SHORT_REAL\n");
			return;
		}
		uint16_t netsh;
		memcpy(&netsh, p, 2);
		printf("SHORT_REAL - %.2f\n", ntohs(netsh) / 100.0);
		break;
	}
	case 2:
	{ // FLOAT
		if (remain < 6)
		{
			fprintf(stderr, "Payload too short for FLOAT\n");
			return;
		}
		uint8_t sign = p[0];
		uint32_t netm;
		memcpy(&netm, p + 1, 4);
		uint8_t exp = p[5];
		double val = (double)ntohl(netm);
		for (int i = 0; i < exp; i++)
			val /= 10.0;
		if (sign)
			val = -val;
		printf("FLOAT - %.*f\n", exp, val);
		break;
	}
	case 3:
	{ // STRING
		size_t sl = strnlen((const char *)p, remain);
		char *s = malloc(sl + 1);
		memcpy(s, p, sl);
		s[sl] = '\0';
		printf("STRING - %s\n", s);
		free(s);
		break;
	}
	default:
		fprintf(stderr, "Unknown payload type: %u\n", type);
	}
}

void print_packet(char *buf, size_t total_len)
{
	char *p = buf, *end = buf + total_len;

	// 1) găseşte ip‑ul (ascii până la primul ' ')
	char ip[INET_ADDRSTRLEN] = {0};
	char *sp = memchr(p, ' ', end - p);
	if (!sp)
		return; // format greșit
	size_t ip_len = sp - p;
	memcpy(ip, p, ip_len);
	ip[ip_len] = '\0';
	p = sp + 1; // acum p e la începutul port

	// 2) găseşte portul (ascii până la următorul ' ')
	char port[6] = {0};
	sp = memchr(p, ' ', end - p);
	if (!sp)
		return;
	size_t port_len = sp - p;
	memcpy(port, p, port_len);
	port[port_len] = '\0';
	p = sp + 1; // acum p e la începutul topic

	// 3) topic-ul e fix 50 octeţi, nu e NUL‑terminated în buffer
	if (p + MAX_TOPIC_LEN > end)
		return;
	char topic[MAX_TOPIC_LEN + 1];
	memcpy(topic, p, MAX_TOPIC_LEN);
	topic[MAX_TOPIC_LEN] = '\0';
	// taie tot de la primul '\0' adevărat
	topic[strnlen(topic, MAX_TOPIC_LEN)] = '\0';
	p += MAX_TOPIC_LEN; // acum p e la byte‑ul data_type

	// 4) restul este payload-ul propriu‑zis
	size_t payload_len = end - p;

	if (payload_len < 1)
		return; // nimic de procesat

	// 5) tipărește prefixul
	printf("%s:%s - %s - ", ip, port, topic);

	// 6) apelează funcția care decodează payload-ul
	process_payload(p, payload_len);
}

int handle_received_data(int sockfd)
{
	MsgHeader hdr;
	ssize_t r = recv_all(sockfd, &hdr, sizeof hdr);
	if (r <= 0)
		return (r == 0 ? 0 : -1);

	uint16_t type = ntohs(hdr.type);
	uint32_t length = ntohl(hdr.length);

	if (length > READ_BUF_SIZE)
	{
		fprintf(stderr, "Payload too large: %u bytes\n", length);
		return -1;
	}

	char buf[READ_BUF_SIZE];
	if (recv_all(sockfd, buf, length) != (ssize_t)length)
	{
		fprintf(stderr, "Short read: got less than %u bytes\n", length);
		return -1;
	}

	switch (type)
	{
	case MSG_PUBLISH:
		print_packet(buf, length);
		break;
	case MSG_SUBSCRIBE_ACK:
		buf[length] = '\0';
		printf("Subscribed to topic %s\n", buf);
		break;
	case MSG_UNSUBSCRIBE_ACK:
		buf[length] = '\0';
		printf("Unsubscribed from topic %s\n", buf);
		break;
	default:
		buf[length] = '\0';
		printf(">> MSG_TYPE %u: %s\n", type, buf);
	}
	return 1;
}

int main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IONBF, 0);

	if (argc != 4)
	{
		fprintf(stderr,
				"Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n",
				argv[0]);
		exit(EXIT_FAILURE);
	}
	const char *client_id = argv[1];
	const char *server_ip = argv[2];
	int server_port = atoi(argv[3]);
	if (server_port <= 0)
	{
		fprintf(stderr, "Invalid port '%s'\n", argv[3]);
		exit(EXIT_FAILURE);
	}

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
	{
		perror("socket");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in serv = {0};
	serv.sin_family = AF_INET;
	serv.sin_port = htons(server_port);
	inet_pton(AF_INET, server_ip, &serv.sin_addr);

	if (connect(sockfd, (struct sockaddr *)&serv, sizeof serv) != 0)
	{
		perror("connect");
		exit(EXIT_FAILURE);
	}

	// send client ID + newline
	char initb[32];
	int L = snprintf(initb, sizeof initb, "%s\n", client_id);
	if (send(sockfd, initb, L, 0) != L)
	{
		perror("send client ID");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	fd_set fds;
	int maxfd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;

	while (1)
	{
		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);
		FD_SET(sockfd, &fds);

		if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0)
		{
			perror("select");
			break;
		}

		// user commands
		if (FD_ISSET(STDIN_FILENO, &fds))
		{
			char line[128];
			if (!fgets(line, sizeof line, stdin))
			{
				// EOF on stdin: just ignore further stdin
				FD_CLR(STDIN_FILENO, &fds);
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

		// server data
		if (FD_ISSET(sockfd, &fds))
		{
			int rc = handle_received_data(sockfd);
			if (rc <= 0)
				break;
		}
	}

	close(sockfd);
	return 0;
}
