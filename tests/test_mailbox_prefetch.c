#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main ch347_dirty_usb_sink_program_main
#include "../src/ch347_dirty_usb_sink.c"
#undef main

int main(void)
{
    const size_t mapping_size = frame_mailbox_size(FRAME_BYTES);
    void *mapping = calloc(1, mapping_size);
    uint8_t *frame = malloc(FRAME_BYTES);
    struct frame_mailbox_header *header = mapping;
    uint64_t consumed_seq = 0;
    unsigned int captured_frames = 0;
    unsigned int dropped_frames = 0;
    unsigned int slot = 1u % FRAME_MAILBOX_SLOTS;
    int prefetched_frame = 1;

    assert(mapping != NULL);
    assert(frame != NULL);
    header->magic = FRAME_MAILBOX_MAGIC;
    header->version = FRAME_MAILBOX_VERSION;
    header->width = LCD_WIDTH;
    header->height = LCD_HEIGHT;
    header->frame_bytes = FRAME_BYTES;
    header->slot_count = FRAME_MAILBOX_SLOTS;
    memset(frame_mailbox_slot(mapping, FRAME_BYTES, slot), 0x5a, FRAME_BYTES);
    atomic_store_explicit(&header->slot_seq[slot], 1, memory_order_release);
    atomic_store_explicit(&header->published_seq, 1, memory_order_release);

    /* Startup waits for and copies the first publication, which advances the
     * sequence before the main loop begins. */
    assert(mailbox_copy_latest(mapping, mapping_size, frame, &consumed_seq,
                &captured_frames, &dropped_frames) == 1);
    assert(consumed_seq == 1);
    assert(captured_frames == 1);
    assert(frame[0] == 0x5a && frame[FRAME_BYTES - 1] == 0x5a);
    assert(mailbox_copy_latest(mapping, mapping_size, frame, &consumed_seq,
                &captured_frames, &dropped_frames) == 0);

    /* The pending startup edge must still make that copied frame look new
     * exactly once; without it a static producer leaves the panel untouched. */
    assert(mailbox_copy_next(mapping, mapping_size, frame, &consumed_seq,
                &captured_frames, &dropped_frames, prefetched_frame) == 1);
    prefetched_frame = 0;
    assert(mailbox_copy_next(mapping, mapping_size, frame, &consumed_seq,
                &captured_frames, &dropped_frames, prefetched_frame) == 0);

    free(frame);
    free(mapping);
    puts("test_mailbox_prefetch: ok");
    return 0;
}
