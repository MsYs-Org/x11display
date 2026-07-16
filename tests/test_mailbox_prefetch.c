#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    struct rect bbox;
    struct debug_overlay_state overlay;
    struct debug_overlay_metrics metrics = {0};
    struct rect overlay_damage[3];
    unsigned int overlay_damage_count;
    size_t overlay_dirty_area;
    char runtime_path[] = "/tmp/msys-sink-runtime-XXXXXX";
    int runtime_fd;
    FILE *runtime_stream;
    struct sink_runtime_config runtime = {
        .debug = 0,
        .cursor_enabled = 0,
        .rotation = FRAME_ROTATION_NORMAL,
        .overlay_enabled = 0,
        .overlay_alpha = 176,
        .overlay_scale = 1,
        .overlay_items = DEBUG_OVERLAY_DEFAULT_ITEMS,
        .overlay_interval_ms = 1000,
    };

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
    assert(rect_list_bbox(measured, 2, &bbox));
    assert(bbox.x0 == 0 && bbox.y0 == 0 && bbox.x1 == 24 && bbox.y1 == 33);

    unsetenv("CH347_DEBUG_OVERLAY");
    unsetenv("CH347_DEBUG_OVERLAY_ALPHA");
    unsetenv("CH347_DEBUG_OVERLAY_SCALE");
    unsetenv("CH347_DEBUG_OVERLAY_ITEMS");
    unsetenv("CH347_DEBUG_OVERLAY_INTERVAL_MS");
    debug_overlay_init(&overlay);
    assert(!overlay.enabled);
    assert(overlay.alpha == 176 && overlay.scale == 1);
    assert(overlay.items == DEBUG_OVERLAY_DEFAULT_ITEMS);
    assert(overlay.interval_ms == 1000);
    assert(!debug_overlay_due(&overlay, 1.0));
    assert(idle_wake_interval_ms(0, 0, 0, 0, &overlay, 1.0) == 0);

    assert(setenv("CH347_DEBUG_OVERLAY", "1", 1) == 0);
    assert(setenv("CH347_DEBUG_OVERLAY_ALPHA", "128", 1) == 0);
    assert(setenv("CH347_DEBUG_OVERLAY_SCALE", "1", 1) == 0);
    assert(setenv("CH347_DEBUG_OVERLAY_ITEMS", "all", 1) == 0);
    assert(setenv("CH347_DEBUG_OVERLAY_INTERVAL_MS", "250", 1) == 0);
    debug_overlay_init(&overlay);
    assert(debug_overlay_due(&overlay, 1.0));
    assert(idle_wake_interval_ms(0, 0, 0, 0, &overlay, 1.0) == 1);
    assert(idle_wake_interval_ms(1, 20, 0, 0, &overlay, 1.0) == 1);
    metrics.capture_fps = 59.5;
    metrics.panel_fps = 8.25;
    metrics.rects = 1;
    metrics.dirty_pct = 12.0;
    metrics.last_sent_pixels = 2048;
    metrics.sent_pixels = 4096;
    metrics.bbox = measured[1];
    metrics.bbox_valid = 1;
    assert(debug_overlay_sample(&overlay, &metrics, 1.0));
    assert(overlay.line_count == 5);
    assert(strstr(overlay.lines[0], "C:59.5") != NULL);
    assert(strstr(overlay.lines[2], "B:4096") != NULL);
    assert(strstr(overlay.lines[3], "Q:20,30-24,33") != NULL);
    assert(strstr(overlay.lines[4], "SINK RSS:") != NULL);
    memset(frame, 0xff, FRAME_BYTES);
    draw_debug_overlay(frame, &overlay);
    assert(frame[0] != 0xff || frame[1] != 0xff);
    assert(frame[((size_t)LCD_HEIGHT - 1) * STRIDE_BYTES] == 0xff);
    assert(!debug_overlay_sample(&overlay, &metrics, 1.20));
    assert(!debug_overlay_due(&overlay, 1.20));
    {
        unsigned int overlay_wait = idle_wake_interval_ms(0, 0, 0, 0,
                &overlay, 1.20);

        assert(overlay_wait >= 50 && overlay_wait <= 51);
    }
    assert(idle_wake_interval_ms(1, 20, 0, 0, &overlay, 1.20) == 20);
    assert(debug_overlay_due(&overlay, 1.25));
    assert(debug_overlay_sample(&overlay, &metrics, 1.26));

    overlay_damage[0] = measured[0];
    overlay_damage[1] = (struct rect){250, 400, 259, 409};
    overlay_damage[2] = overlay.bounds;
    overlay_damage_count = 3;
    assert(collapse_overlay_damage_to_single_bbox(overlay_damage,
                &overlay_damage_count, 1, 1));
    assert(overlay_damage_count == 1);
    assert(overlay_damage[0].x0 == 0 && overlay_damage[0].y0 == 0);
    assert(overlay_damage[0].x1 == 259 && overlay_damage[0].y1 == 409);

    overlay_damage[0] = measured[0];
    overlay_damage[1] = measured[1];
    overlay_damage_count = 2;
    assert(!collapse_overlay_damage_to_single_bbox(overlay_damage,
                &overlay_damage_count, 1, 0));
    assert(overlay_damage_count == 2);

    overlay_damage_count = 0;
    overlay_dirty_area = 0;
    assert(select_overlay_idle_damage(overlay_damage, &overlay_damage_count,
                &overlay_dirty_area, &overlay, 1, 1, 0, 0, 0));
    assert(overlay_damage_count == 1);
    assert(memcmp(&overlay_damage[0], &overlay.bounds,
                sizeof(overlay.bounds)) == 0);
    assert(overlay_dirty_area == rect_pixels(&overlay.bounds));
    assert(!select_overlay_idle_damage(overlay_damage, &overlay_damage_count,
                &overlay_dirty_area, &overlay, 1, 1, 1, 0, 0));

    runtime_fd = mkstemp(runtime_path);
    assert(runtime_fd >= 0);
    runtime_stream = fdopen(runtime_fd, "w");
    assert(runtime_stream != NULL);
    assert(fputs(
                "DEBUG=1\n"
                "CH347_DEBUG_OVERLAY=1\n"
                "CH347_DEBUG_OVERLAY_ALPHA=128\n"
                "CH347_DEBUG_OVERLAY_SCALE=2\n"
                "CH347_DEBUG_OVERLAY_ITEMS=25\n"
                "CH347_DEBUG_OVERLAY_INTERVAL_MS=750\n"
                "CH347_CURSOR=1\n"
                "CH347_DISPLAY_ROTATION=right\n",
                runtime_stream) >= 0);
    assert(fclose(runtime_stream) == 0);
    assert(sink_runtime_config_load(&runtime, runtime_path, runtime_path,
                runtime_path, runtime_path));
    assert(runtime.debug == 1);
    assert(runtime.cursor_enabled == 1);
    assert(runtime.rotation == FRAME_ROTATION_RIGHT);
    assert(runtime.overlay_enabled == 1);
    assert(runtime.overlay_alpha == 128);
    assert(runtime.overlay_scale == 2);
    assert(runtime.overlay_items == 25);
    assert(runtime.overlay_interval_ms == 750);
    runtime_stream = fopen(runtime_path, "w");
    assert(runtime_stream != NULL);
    assert(fputs("DEBUG=invalid\n", runtime_stream) >= 0);
    assert(fclose(runtime_stream) == 0);
    assert(!sink_runtime_config_load(&runtime, runtime_path, runtime_path,
                runtime_path, runtime_path));
    assert(runtime.debug == 1 && runtime.rotation == FRAME_ROTATION_RIGHT);
    assert(unlink(runtime_path) == 0);

    assert(setenv("CH347_DEBUG_OVERLAY_ITEMS", "unknown", 1) == 0);
    debug_overlay_init(&overlay);
    assert(overlay.items == DEBUG_OVERLAY_DEFAULT_ITEMS);
    assert(reset_usb_device("relative") == 64);
    assert(reset_usb_device("/dev/null") == 1);

    free(frame);
    free(mapping);
    puts("test_mailbox_prefetch: ok");
    return 0;
}
