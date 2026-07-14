#ifndef MSYS_FRAME_ROTATION_H
#define MSYS_FRAME_ROTATION_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum frame_rotation {
    FRAME_ROTATION_NORMAL = 0,
    FRAME_ROTATION_RIGHT,
    FRAME_ROTATION_INVERTED,
    FRAME_ROTATION_LEFT,
};

static inline const char *frame_rotation_name(enum frame_rotation rotation)
{
    switch (rotation) {
    case FRAME_ROTATION_RIGHT:
        return "right";
    case FRAME_ROTATION_INVERTED:
        return "inverted";
    case FRAME_ROTATION_LEFT:
        return "left";
    default:
        return "normal";
    }
}

static inline int frame_rotation_parse(const char *value,
        enum frame_rotation *rotation)
{
    if (!rotation)
        return 0;
    if (!value || !*value || strcmp(value, "normal") == 0 ||
            strcmp(value, "portrait") == 0) {
        *rotation = FRAME_ROTATION_NORMAL;
        return 1;
    }
    if (strcmp(value, "right") == 0 || strcmp(value, "clockwise") == 0 ||
            strcmp(value, "landscape") == 0) {
        *rotation = FRAME_ROTATION_RIGHT;
        return 1;
    }
    if (strcmp(value, "inverted") == 0 || strcmp(value, "180") == 0) {
        *rotation = FRAME_ROTATION_INVERTED;
        return 1;
    }
    if (strcmp(value, "left") == 0 ||
            strcmp(value, "counter-clockwise") == 0) {
        *rotation = FRAME_ROTATION_LEFT;
        return 1;
    }
    return 0;
}

static inline void frame_rotation_output_size(enum frame_rotation rotation,
        unsigned int source_width, unsigned int source_height,
        unsigned int *output_width, unsigned int *output_height)
{
    if (rotation == FRAME_ROTATION_LEFT || rotation == FRAME_ROTATION_RIGHT) {
        *output_width = source_height;
        *output_height = source_width;
    } else {
        *output_width = source_width;
        *output_height = source_height;
    }
}

static inline void frame_rotate_rgb565be(const uint8_t *source,
        unsigned int source_width, unsigned int source_height, uint8_t *output,
        enum frame_rotation rotation)
{
    unsigned int output_width;
    unsigned int output_height;
    unsigned int y;

    frame_rotation_output_size(rotation, source_width, source_height,
            &output_width, &output_height);
    if (rotation == FRAME_ROTATION_NORMAL) {
        memcpy(output, source, (size_t)source_width * source_height * 2u);
        return;
    }
    for (y = 0; y < source_height; y++) {
        unsigned int x;

        for (x = 0; x < source_width; x++) {
            unsigned int destination_x;
            unsigned int destination_y;
            size_t source_offset = ((size_t)y * source_width + x) * 2u;
            size_t output_offset;

            if (rotation == FRAME_ROTATION_RIGHT) {
                destination_x = source_height - 1u - y;
                destination_y = x;
            } else if (rotation == FRAME_ROTATION_LEFT) {
                destination_x = y;
                destination_y = source_width - 1u - x;
            } else {
                destination_x = source_width - 1u - x;
                destination_y = source_height - 1u - y;
            }
            output_offset = ((size_t)destination_y * output_width +
                    destination_x) * 2u;
            output[output_offset] = source[source_offset];
            output[output_offset + 1u] = source[source_offset + 1u];
        }
    }
    (void)output_height;
}

/* Convert a physical panel coordinate back into the logical X11 coordinate
 * whose framebuffer was rotated into that panel. */
static inline void frame_rotation_unmap_point(enum frame_rotation rotation,
        unsigned int physical_width, unsigned int physical_height,
        int physical_x, int physical_y, int *logical_x, int *logical_y)
{
    switch (rotation) {
    case FRAME_ROTATION_RIGHT:
        *logical_x = physical_y;
        *logical_y = (int)physical_width - 1 - physical_x;
        break;
    case FRAME_ROTATION_LEFT:
        *logical_x = (int)physical_height - 1 - physical_y;
        *logical_y = physical_x;
        break;
    case FRAME_ROTATION_INVERTED:
        *logical_x = (int)physical_width - 1 - physical_x;
        *logical_y = (int)physical_height - 1 - physical_y;
        break;
    default:
        *logical_x = physical_x;
        *logical_y = physical_y;
        break;
    }
}

#endif
