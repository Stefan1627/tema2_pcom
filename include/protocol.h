#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <unistd.h>

// — message types —
#define MSG_SUBSCRIBE   1
#define MSG_UNSUBSCRIBE 2
#define MSG_PUBLISH     3

// packed 2‑byte type + 4‑byte payload length
#pragma pack(push,1)
typedef struct {
    uint16_t type;
    uint32_t length;
} MsgHeader;
#pragma pack(pop)

// send() until everything’s written
int send_all(int fd, const void *buf, size_t len);

// build header + payload, then send both
int send_message(int fd, uint16_t type,
                 const void *payload, uint32_t len);

#endif // PROTOCOL_H
