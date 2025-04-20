#include "../include/client_server.h"
#include "../include/protocol.h"
#include "../include/topic_trie.h"

#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define MAX_UDP_PAYLOAD 1500
#define MAX_TOPIC_LEN 50

char *extract_topic(char *msg) {
    if (!msg) return NULL;

    char *topic = malloc(MAX_TOPIC_LEN + 1);
    if (!topic) return NULL;

    int i;
    for (i = 0; i < MAX_TOPIC_LEN && msg[i] != '\0'; i++) {
        topic[i] = msg[i];
    }
    topic[i] = '\0';

	size_t rem_len = strlen(msg + i + 1);
    memmove(msg, msg + i + 1, rem_len + 1);

    return topic;
}

void run_server(int port) {
    // set up UDP socket
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) { perror("socket udp"); exit(1); }

    // set up TCP socket
    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) { perror("socket tcp"); exit(1); }

    // reuse addr
    int one = 1;
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    bind(udp_fd, (struct sockaddr*)&addr, sizeof(addr));
    bind(tcp_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(tcp_fd, SOMAXCONN);

    client_t *clients = NULL;
    int        client_count = 0;
    int        exit_flag    = 0;
	trie_init();

    while (!exit_flag) {
        int nfds = 3 + client_count;
        struct pollfd *pfds = malloc(nfds * sizeof(*pfds));

        // index 0 = stdin, 1 = udp, 2 = tcp
        pfds[0] = (struct pollfd){ .fd = STDIN_FILENO, .events = POLLIN };
        pfds[1] = (struct pollfd){ .fd = udp_fd,       .events = POLLIN };
        pfds[2] = (struct pollfd){ .fd = tcp_fd,       .events = POLLIN };

        int idx = 3;
        for (client_t *c = clients; c; c = c->next, idx++) {
            pfds[idx] = (struct pollfd){ .fd = c->fd, .events = POLLIN };
        }

        if (poll(pfds, nfds, -1) < 0) {
            perror("poll"); free(pfds); break;
        }

        idx = 0;
        // — exit on stdin
        if (pfds[idx++].revents & POLLIN) {
            char buf[32];
            if (fgets(buf, sizeof(buf), stdin) &&
                strcmp(buf, "exit\n") == 0)
                exit_flag = 1;
        }

        // — incoming UDP?
        if (pfds[idx].revents & POLLIN) {
            char buf[MAX_UDP_PAYLOAD];
            struct sockaddr_in src;
            socklen_t slen = sizeof(src);
            ssize_t n = recvfrom(udp_fd, buf, sizeof(buf), 0,
                                 (struct sockaddr*)&src, &slen);
            if (n > 0) {
				char *topic = extract_topic(buf);
                trie_publish(topic, buf, n);
            }
        }
        idx++;

        // — new TCP connection?
        if (pfds[idx].revents & POLLIN) {
            struct sockaddr_in cli;
            socklen_t clilen = sizeof(cli);
            int newfd = accept(tcp_fd,
                               (struct sockaddr*)&cli,
                               &clilen);
            if (newfd >= 0) {
                // read client ID once
                char id[16];
                ssize_t len = recv(newfd, id, sizeof(id)-1, 0);
                if (len > 0) {
                    id[len] = '\0';
                    id[strcspn(id, "\r\n")] = '\0';

                    // check duplicate
                    bool dup = false;
                    for (client_t *it = clients; it; it = it->next) {
                        if (strcmp(it->id, id) == 0) { dup = true; break; }
                    }
                    if (!dup) {
                        client_t *nc = client_create(newfd, id);
                        if (nc) {
                            nc->next = clients;
                            clients  = nc;
                            client_count++;
                            fprintf(stdout, "New client %s connected from %s:%d.\n",
								nc->id,
								inet_ntoa(cli.sin_addr),
								ntohs(cli.sin_port));
                        }
                    } else {
						printf("Client %s already connected.\n", id);
                        close(newfd);
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
            if (pfds[idx].revents & (POLLIN|POLLHUP|POLLERR)) {
                if (client_handle_data(cur) < 0) {
                    // tear down
                    printf("Client %s disconnected.\n", cur->id);
                    client_count--;
                    client_t *dead = cur;
                    cur = cur->next;
                    if (prev) prev->next = cur;
                    else       clients   = cur;
                    client_destroy(dead);
                    idx++;
                    continue;
                }
            }
            prev = cur;
            cur  = cur->next;
            idx++;
        }

        free(pfds);
    }

    // cleanup
    for (client_t *c = clients; c; ) {
        client_t *tmp = c;
        c = c->next;
        client_destroy(tmp);
    }
    close(tcp_fd);
    close(udp_fd);
}

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    run_server(atoi(argv[1]));
    return 0;
}
