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
    struct rect measured[2] = {
        {0, 0, 9, 9},
        {20, 30, 24, 33},
    };
    struct rect full = {0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1};

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

    /* While one SPI rectangle is in flight the producer may wrap all mailbox
     * slots.  The next consumer pass must jump straight to the newest complete
     * publication instead of replaying any intermediate position. */
    for (uint64_t seq = 2; seq <= 7; seq++) {
        slot = (unsigned int)(seq % FRAME_MAILBOX_SLOTS);
        memset(frame_mailbox_slot(mapping, FRAME_BYTES, slot), (int)seq,
                FRAME_BYTES);
        atomic_store_explicit(&header->slot_seq[slot], seq,
                memory_order_release);
        atomic_store_explicit(&header->published_seq, seq,
                memory_order_release);
    }
    assert(mailbox_copy_latest(mapping, mapping_size, frame, &consumed_seq,
                &captured_frames, &dropped_frames) == 1);
    assert(consumed_seq == 7);
    assert(captured_frames == 7);
    assert(dropped_frames == 5);
    assert(frame[0] == 7 && frame[FRAME_BYTES - 1] == 7);

    /* Refresh accounting reports the physical rectangle payload rather than
     * the rounded percentage printed by the legacy debug line. */
    assert(rect_list_pixels(measured, 2) == 120);
    assert(!rect_list_has_full_refresh(measured, 2));
    assert(rect_list_pixels(&full, 1) == FRAME_PIXELS);
    assert(rect_list_has_full_refresh(&full, 1));
    assert(reset_usb_device("relative") == 64);
    assert(reset_usb_device("/dev/null") == 1);

    free(frame);
    free(mapping);
    puts("test_mailbox_prefetch: ok");
    return 0;
}
