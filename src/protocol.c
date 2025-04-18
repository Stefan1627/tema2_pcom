#include "../include/protocol.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>

int send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf;
    size_t left = len;
    while (left) {
        ssize_t w = send(fd, p, left, 0);
        if (w <= 0) return -1;
        p    += w;
        left -= w;
    }
    return 0;
}

int send_message(int fd, uint16_t type,
                 const void *payload, uint32_t len)
{
    MsgHeader hdr;
    hdr.type   = htons(type);
    hdr.length = htonl(len);

    if (send_all(fd, &hdr, sizeof(hdr)) < 0)       return -1;
    if (len > 0 && send_all(fd, payload, len) < 0) return -1;
    return 0;
}
