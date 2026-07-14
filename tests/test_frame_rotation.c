#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "frame_rotation.h"

static uint16_t pixel(const uint8_t *frame, unsigned int width,
        unsigned int x, unsigned int y)
{
    size_t offset = ((size_t)y * width + x) * 2u;

    return (uint16_t)((uint16_t)frame[offset] << 8 | frame[offset + 1u]);
}

static void test_rotation(void)
{
    const uint8_t source[] = {
        0, 1, 0, 2, 0, 3,
        0, 4, 0, 5, 0, 6,
    };
    uint8_t output[sizeof(source)];

    frame_rotate_rgb565be(source, 3, 2, output, FRAME_ROTATION_RIGHT);
    assert(pixel(output, 2, 0, 0) == 4);
    assert(pixel(output, 2, 1, 0) == 1);
    assert(pixel(output, 2, 0, 2) == 6);
    assert(pixel(output, 2, 1, 2) == 3);

    frame_rotate_rgb565be(source, 3, 2, output, FRAME_ROTATION_LEFT);
    assert(pixel(output, 2, 0, 0) == 3);
    assert(pixel(output, 2, 1, 0) == 6);
    assert(pixel(output, 2, 0, 2) == 1);
    assert(pixel(output, 2, 1, 2) == 4);

    frame_rotate_rgb565be(source, 3, 2, output, FRAME_ROTATION_INVERTED);
    assert(pixel(output, 3, 0, 0) == 6);
    assert(pixel(output, 3, 2, 1) == 1);
}

static void test_touch_inverse(void)
{
    int x;
    int y;

    frame_rotation_unmap_point(FRAME_ROTATION_RIGHT, 320, 480, 0, 0, &x, &y);
    assert(x == 0 && y == 319);
    frame_rotation_unmap_point(FRAME_ROTATION_RIGHT, 320, 480, 319, 479, &x, &y);
    assert(x == 479 && y == 0);
    frame_rotation_unmap_point(FRAME_ROTATION_LEFT, 320, 480, 0, 0, &x, &y);
    assert(x == 479 && y == 0);
    frame_rotation_unmap_point(FRAME_ROTATION_INVERTED, 320, 480, 0, 0, &x, &y);
    assert(x == 319 && y == 479);
}

int main(void)
{
    enum frame_rotation rotation;
    unsigned int width;
    unsigned int height;

    assert(frame_rotation_parse("landscape", &rotation));
    assert(rotation == FRAME_ROTATION_RIGHT);
    assert(!frame_rotation_parse("diagonal", &rotation));
    frame_rotation_output_size(FRAME_ROTATION_RIGHT, 480, 320, &width, &height);
    assert(width == 320 && height == 480);
    test_rotation();
    test_touch_inverse();
    puts("test_frame_rotation: ok");
    return 0;
}
