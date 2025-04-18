// subscriber.c
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

int main(int argc, char *argv[]) {
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *client_id = argv[1];
    const char *server_ip = argv[2];
    int server_port = atoi(argv[3]);
    if (server_port <= 0) {
        fprintf(stderr, "Error: invalid port number '%s'\n", argv[3]);
        exit(EXIT_FAILURE);
    }

    // Create TCP socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Build server address and connect
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(server_port);
	serv_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))
        != 0) {
        printf("connection with the server failed...\n");
        exit(0);
    }
    else
        printf("connected to the server..\n");

    // Send client ID (terminated by '\n')
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%s\n", client_id);
    if (len < 0 || len >= (int)sizeof(buf)) {
        fprintf(stderr, "Error: client ID too long\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    if (send(sockfd, buf, len, 0) != len) {
        perror("send");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // --- Now keep the connection open, watch for "exit" on stdin ---
    fd_set read_fds;
    int maxfd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sockfd,      &read_fds);

        if (select(maxfd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        // Handle user typing "exit"
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char line[128];
            if (!fgets(line, sizeof(line), stdin)) {
                break;
            }
            // strip newline
            line[strcspn(line, "\n")] = '\0';
            if (strcmp(line, "exit") == 0) {
                // clean shutdown
                break;
            }
            // (later: handle "subscribe"/"unsubscribe")
        }

        // Handle data from server (none yet)
        if (FD_ISSET(sockfd, &read_fds)) {
            char dummy[1500];
            ssize_t r = recv(sockfd, dummy, sizeof(dummy), 0);
            if (r <= 0) {
                // server closed connection
                break;
            }
            // (later: parse and print server‑forwarded UDP messages)
        }
    }

    close(sockfd);
    return 0;
}
