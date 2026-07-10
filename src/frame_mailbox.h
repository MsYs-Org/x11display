#ifndef X11DISPLAY_FRAME_MAILBOX_H
#define X11DISPLAY_FRAME_MAILBOX_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_MAILBOX_MAGIC 0x58464d42u
#define FRAME_MAILBOX_VERSION 2u
#define FRAME_MAILBOX_SLOTS 3u
#define FRAME_MAILBOX_HEADER_BYTES 4096u

struct frame_mailbox_header {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t frame_bytes;
    uint32_t slot_count;
    _Atomic uint64_t published_seq;
    _Atomic uint64_t consumed_seq;
    _Atomic uint32_t producer_alive;
    _Atomic uint64_t producer_heartbeat_ms;
    _Atomic uint64_t slot_seq[FRAME_MAILBOX_SLOTS];
};

static inline size_t frame_mailbox_size(size_t frame_bytes)
{
    return FRAME_MAILBOX_HEADER_BYTES + FRAME_MAILBOX_SLOTS * frame_bytes;
}

static inline uint8_t *frame_mailbox_slot(void *mapping, size_t frame_bytes,
        unsigned int slot)
{
    return (uint8_t *)mapping + FRAME_MAILBOX_HEADER_BYTES +
        (size_t)slot * frame_bytes;
}

#endif
