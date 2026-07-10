#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define GPIO_IRQ 3
#define GPIO_MASK(bit) (1u << (bit))

static double now_sec(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int read_gpio(int fd, uint8_t *pins, uint8_t raw[8])
{
    uint8_t packet[11] = {0};
    uint8_t reply[64] = {0};
    ssize_t ret;

    packet[0] = 0xCC;
    packet[1] = 0x08;
    packet[2] = 0x00;

    ret = write(fd, packet, sizeof(packet));
    if (ret != (ssize_t)sizeof(packet))
        return -1;

    for (unsigned int tries = 0; tries < 128; tries++) {
        ret = read(fd, reply, sizeof(reply));
        if (ret <= 0)
            return -1;
        if (reply[0] == 0xCC)
            break;
    }
    if (reply[0] != 0xCC || ret < 11)
        return -1;

    *pins = 0;
    for (unsigned int i = 0; i < 8; i++) {
        raw[i] = reply[3 + i];
        if (raw[i] & 0x40)
            *pins |= GPIO_MASK(i);
    }

    return 0;
}

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/ch34x_pis0";
    unsigned int ms = argc > 2 ? (unsigned int)strtoul(argv[2], NULL, 0) : 20;
    unsigned int seconds = argc > 3 ? (unsigned int)strtoul(argv[3], NULL, 0) : 15;
    int fd;
    uint8_t pins = 0;
    uint8_t raw[8] = {0};
    int last = -1;
    unsigned int high = 0;
    unsigned int low = 0;
    unsigned int edges = 0;
    int stuck_level = -1;
    double start;
    double last_edge_time;

    if (!ms)
        ms = 20;

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", dev, strerror(errno));
        return 1;
    }

    fprintf(stderr, "watching GPIO3 active-low IRQ on %s for %us, poll=%ums\n",
            dev, seconds, ms);
    fprintf(stderr, "press/release the panel several times; GPIO3 should go 1->0 while pressed and 0->1 on release\n");
    start = now_sec();
    last_edge_time = start;
    while (now_sec() - start < seconds) {
        int irq_high;
        double now;

        if (read_gpio(fd, &pins, raw) < 0) {
            fprintf(stderr, "gpio read failed\n");
            close(fd);
            return 1;
        }

        now = now_sec();
        irq_high = !!(pins & GPIO_MASK(GPIO_IRQ));
        if (irq_high)
            high++;
        else
            low++;

        if (last < 0 || irq_high != last) {
            if (last >= 0)
                edges++;
            last_edge_time = now;
            stuck_level = -1;
            printf("%.3f GPIO=0x%02x GPIO3=%d raw3=0x%02x %s\n",
                    now - start, pins, irq_high, raw[GPIO_IRQ],
                    irq_high ? "released/high" : "pressed/low");
            fflush(stdout);
            last = irq_high;
        }

        if (stuck_level != irq_high && last >= 0 && now - last_edge_time > 3.0) {
            printf("%.3f no GPIO3 edge for %.1fs, current=%d raw3=0x%02x %s\n",
                    now - start, now - last_edge_time, irq_high, raw[GPIO_IRQ],
                    irq_high ? "idle_or_irq_not_wired" : "stuck_low_or_still_pressed");
            fflush(stdout);
            stuck_level = irq_high;
        }

        usleep(ms * 1000);
    }

    printf("summary high=%u low=%u edges=%u irq_%s\n", high, low, edges,
            (high && low && edges) ? "usable" :
            (low && !high) ? "stuck_low" :
            (!low && high) ? "never_low" : "unknown");
    close(fd);
    return 0;
}
