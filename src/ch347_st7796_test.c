#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "CH347LIB.h"

#define LCD_WIDTH  320
#define LCD_HEIGHT 480

#define LCD_CS 0x80
#define SPI_PACKET_MAX_PAYLOAD 480
#define SPI_BIG_MAX_PAYLOAD 4092
#define CH34x_LCD_FRAME_WRITE 0x80000020UL
#define CH34x_LCD_FRAME_WRITE_WINDOW 0x80000022UL

#define CH347_CS0_ENABLE 0x0001
#define CH347_CS1_ENABLE 0x0100
#define CH347_CS0_HIGH   0x0001
#define CH347_CS1_HIGH   0x0100

#define GPIO_DC    0
#define GPIO_RESET 1
#define GPIO_LED   2

#define GPIO_MASK(bit) (1u << (bit))
#define GPIO_OUTPUT_MASK (GPIO_MASK(GPIO_DC) | GPIO_MASK(GPIO_RESET) | GPIO_MASK(GPIO_LED))

static int dev = -1;
static uint8_t gpio_state = 0;
static size_t raw_spi_payload = SPI_PACKET_MAX_PAYLOAD;
static unsigned int raw_spi_delay_us = 0;
static const char *transfer_method = "raw";

static void msleep(unsigned int ms)
{
    usleep(ms * 1000);
}

static double now_sec(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int gpio_apply(void)
{
    uint8_t packet[11] = {0};
    uint8_t reply[19] = {0};
    uint8_t enabled = GPIO_OUTPUT_MASK;
    ssize_t ret;

    packet[0] = 0xCC;
    packet[1] = 0x08;
    packet[2] = 0x00;

    for (unsigned int i = 0; i < 8; i++) {
        if (!(enabled & GPIO_MASK(i)))
            continue;

        packet[3 + i] = 0x80 | 0x40 | 0x20 | 0x10;
        if (gpio_state & GPIO_MASK(i))
            packet[3 + i] |= 0x08;
    }

    ret = write(dev, packet, sizeof(packet));
    if (ret != (ssize_t)sizeof(packet)) {
        fprintf(stderr, "GPIO write failed: %zd\n", ret);
        return -1;
    }

    ret = read(dev, reply, sizeof(reply));
    if (ret < 11) {
        fprintf(stderr, "GPIO read failed: %zd\n", ret);
        return -1;
    }

    return 0;
}

static int gpio_write(unsigned int bit, int high)
{
    if (high)
        gpio_state |= GPIO_MASK(bit);
    else
        gpio_state &= (uint8_t)~GPIO_MASK(bit);
    return gpio_apply();
}

static int spi_read_acks(unsigned int packets)
{
    uint8_t ack[512];
    unsigned int reads = (packets + 7) / 8;

    if (!reads)
        reads = 1;

    while (reads--) {
        ssize_t ret;

        ret = read(dev, ack, sizeof(ack));
        if (ret <= 0) {
            fprintf(stderr, "SPI ack read failed: %zd\n", ret);
            return -1;
        }
    }

    return 0;
}

static int spi_write_bytes(const uint8_t *buf, size_t len)
{
	uint8_t *packet;
	size_t off = 0;

	if (!strcmp(transfer_method, "write")) {
		while (off < len) {
			ULONG chunk = (ULONG)(len - off);

			if (chunk > raw_spi_payload)
				chunk = raw_spi_payload;
			if (chunk > SPI_PACKET_MAX_PAYLOAD)
				chunk = SPI_PACKET_MAX_PAYLOAD;

			if (!CH347SPI_Write(dev, LCD_CS, chunk, chunk, (void *)(buf + off))) {
				fprintf(stderr, "CH347SPI_Write failed at offset %zu length %lu\n",
				        off, (unsigned long)chunk);
				return -1;
			}

			off += chunk;
			if (raw_spi_delay_us)
				usleep(raw_spi_delay_us);
		}

		return 0;
	}

	if (!strcmp(transfer_method, "writebig")) {
		ULONG step = (ULONG)raw_spi_payload;

		if (!step)
			step = SPI_PACKET_MAX_PAYLOAD;
		if (step > SPI_BIG_MAX_PAYLOAD)
			step = SPI_BIG_MAX_PAYLOAD;

		if (!CH347SPI_Write(dev, LCD_CS, (ULONG)len, step, (void *)buf)) {
			fprintf(stderr, "CH347SPI_Write big failed length %zu step %lu\n",
			        len, (unsigned long)step);
			return -1;
		}
		if (raw_spi_delay_us)
			usleep(raw_spi_delay_us);

		return 0;
	}

	if (!strcmp(transfer_method, "official")) {
		ULONG step = (ULONG)raw_spi_payload;

		if (!step)
			step = SPI_PACKET_MAX_PAYLOAD;
		if (step > SPI_BIG_MAX_PAYLOAD)
			step = SPI_BIG_MAX_PAYLOAD;

		if (!CH347SPI_Write(dev, LCD_CS, (ULONG)len, step, (void *)buf)) {
			fprintf(stderr, "official CH347SPI_Write failed length %zu step %lu\n",
			        len, (unsigned long)step);
			return -1;
		}
		if (raw_spi_delay_us)
			usleep(raw_spi_delay_us);

		return 0;
	}

	if (!strcmp(transfer_method, "rawbatch")) {
		uint8_t *batch;
		size_t batch_len = 0;
		unsigned int batch_packets = 0;

		batch = malloc(4096);
		if (!batch)
			return -1;

		while (off < len) {
			size_t chunk = len - off;
			ssize_t ret;

			if (chunk > raw_spi_payload)
				chunk = raw_spi_payload;
			if (chunk > SPI_PACKET_MAX_PAYLOAD)
				chunk = SPI_PACKET_MAX_PAYLOAD;

			if (batch_len && batch_len + chunk + 3 > 4096) {
				ret = write(dev, batch, batch_len);
				if (ret != (ssize_t)batch_len) {
					fprintf(stderr, "rawbatch SPI write failed at offset %zu: %zd\n", off, ret);
					free(batch);
					return -1;
				}
				if (spi_read_acks(batch_packets) < 0) {
					free(batch);
					return -1;
				}
				batch_len = 0;
				batch_packets = 0;
			}

			batch[batch_len++] = 0xC4;
			batch[batch_len++] = chunk & 0xff;
			batch[batch_len++] = (chunk >> 8) & 0xff;
			memcpy(batch + batch_len, buf + off, chunk);
			batch_len += chunk;
			batch_packets++;
			off += chunk;
		}

		if (batch_len) {
			ssize_t ret = write(dev, batch, batch_len);
			if (ret != (ssize_t)batch_len) {
				fprintf(stderr, "rawbatch SPI final write failed: %zd\n", ret);
				free(batch);
				return -1;
			}
			if (spi_read_acks(batch_packets) < 0) {
				free(batch);
				return -1;
			}
		}

		free(batch);
		return 0;
	}

	if (!strcmp(transfer_method, "rawburst")) {
		uint8_t *batch;
		size_t batch_len = 0;

		batch = malloc(4096);
		if (!batch)
			return -1;

		while (off < len) {
			size_t chunk = len - off;
			ssize_t ret;

			if (chunk > raw_spi_payload)
				chunk = raw_spi_payload;
			if (chunk > SPI_PACKET_MAX_PAYLOAD)
				chunk = SPI_PACKET_MAX_PAYLOAD;

			if (batch_len && batch_len + chunk + 3 > 4096) {
				ret = write(dev, batch, batch_len);
				if (ret != (ssize_t)batch_len) {
					fprintf(stderr, "rawburst SPI write failed at offset %zu: %zd\n", off, ret);
					free(batch);
					return -1;
				}
				batch_len = 0;
			}

			batch[batch_len++] = 0xC4;
			batch[batch_len++] = chunk & 0xff;
			batch[batch_len++] = (chunk >> 8) & 0xff;
			memcpy(batch + batch_len, buf + off, chunk);
			batch_len += chunk;
			off += chunk;
		}

		if (batch_len) {
			ssize_t ret = write(dev, batch, batch_len);
			if (ret != (ssize_t)batch_len) {
				fprintf(stderr, "rawburst SPI final write failed: %zd\n", ret);
				free(batch);
				return -1;
			}
		}

		free(batch);
		return 0;
	}

	if (!strcmp(transfer_method, "rw")) {
		uint8_t *tmp = malloc(raw_spi_payload);

		if (!tmp)
			return -1;

		while (off < len) {
			ULONG chunk = (ULONG)(len - off);

			if (chunk > raw_spi_payload)
				chunk = raw_spi_payload;

			memcpy(tmp, buf + off, chunk);
			if (!CH347SPI_WriteRead(dev, LCD_CS, chunk, tmp)) {
				fprintf(stderr, "CH347SPI_WriteRead failed at offset %zu length %lu\n",
				        off, (unsigned long)chunk);
				free(tmp);
				return -1;
			}

			off += chunk;
			if (raw_spi_delay_us)
				usleep(raw_spi_delay_us);
		}

		free(tmp);
		return 0;
	}

	packet = malloc(raw_spi_payload + 3);
	if (!packet)
		return -1;

	while (off < len) {
		size_t chunk = len - off;
		uint8_t ack[64];
		ssize_t ret;

        if (chunk > raw_spi_payload)
            chunk = raw_spi_payload;
        if (chunk > SPI_PACKET_MAX_PAYLOAD)
            chunk = SPI_PACKET_MAX_PAYLOAD;

        packet[0] = 0xC4;
        packet[1] = chunk & 0xff;
        packet[2] = (chunk >> 8) & 0xff;
        memcpy(packet + 3, buf + off, chunk);

        ret = write(dev, packet, chunk + 3);
		if (ret != (ssize_t)(chunk + 3)) {
			fprintf(stderr, "raw SPI write failed at offset %zu: %zd\n", off, ret);
			return -1;
		}

		if (!strcmp(transfer_method, "rawack")) {
			ret = read(dev, ack, sizeof(ack));
			if (ret <= 0) {
				fprintf(stderr, "raw SPI ack failed at offset %zu: %zd errno %d\n",
				        off, ret, errno);
				free(packet);
				return -1;
			}
		}

		off += chunk;
		if (raw_spi_delay_us)
			usleep(raw_spi_delay_us);
    }

    free(packet);
    return 0;
}

static int lcd_select(int active)
{
    USHORT enable = CH347_CS0_ENABLE | CH347_CS1_ENABLE;
    USHORT state = CH347_CS1_HIGH;

    if (!strcmp(transfer_method, "official"))
        return 0;

    if (!active)
        state |= CH347_CS0_HIGH;

    if (!CH347SPI_SetChipSelect(dev, enable, state, 0, 0, 0)) {
        fprintf(stderr, "CH347SPI_SetChipSelect(%d) failed\n", active);
        return -1;
    }
    return 0;
}

static int lcd_cmd(uint8_t cmd)
{
    if (gpio_write(GPIO_DC, 0) < 0)
        return -1;
    if (lcd_select(1) < 0)
        return -1;
    if (spi_write_bytes(&cmd, 1) < 0)
        return -1;
    return lcd_select(0);
}

static int lcd_data(const uint8_t *data, size_t len)
{
    if (!len)
        return 0;
    if (gpio_write(GPIO_DC, 1) < 0)
        return -1;
    if (lcd_select(1) < 0)
        return -1;
    if (spi_write_bytes(data, len) < 0)
        return -1;
    return lcd_select(0);
}

static int lcd_cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    if (lcd_cmd(cmd) < 0)
        return -1;
    return lcd_data(data, len);
}

static int lcd_set_pixfmt(uint8_t fmt)
{
    return lcd_cmd_data(0x3A, &fmt, 1);
}

static int lcd_reset(void)
{
    if (gpio_write(GPIO_LED, 0) < 0)
        return -1;
    if (gpio_write(GPIO_RESET, 1) < 0)
        return -1;
    msleep(20);
    if (gpio_write(GPIO_RESET, 0) < 0)
        return -1;
    msleep(30);
    if (gpio_write(GPIO_RESET, 1) < 0)
        return -1;
    msleep(150);
    return 0;
}

static int lcd_init(void)
{
    const uint8_t f0_c3[] = {0xC3};
    const uint8_t f0_96[] = {0x96};
    const uint8_t madctl[] = {0x48};
    const uint8_t pixfmt[] = {0x55};
    const uint8_t invctl[] = {0x01};
    const uint8_t disfun[] = {0x80, 0x02, 0x3B};
    const uint8_t e8[] = {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33};
    const uint8_t c1[] = {0x06};
    const uint8_t c2[] = {0xA7};
    const uint8_t c5[] = {0x18};
    const uint8_t pgamma[] = {
        0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F,
        0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B
    };
    const uint8_t ngamma[] = {
        0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B,
        0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B
    };
    const uint8_t f0_3c[] = {0x3C};
    const uint8_t f0_69[] = {0x69};

    if (lcd_reset() < 0)
        return -1;

    if (lcd_cmd(0x01) < 0)
        return -1;
    msleep(150);

    if (lcd_cmd_data(0xF0, f0_c3, sizeof(f0_c3)) < 0) return -1;
    if (lcd_cmd_data(0xF0, f0_96, sizeof(f0_96)) < 0) return -1;
    if (lcd_cmd_data(0x36, madctl, sizeof(madctl)) < 0) return -1;
    if (lcd_cmd_data(0x3A, pixfmt, sizeof(pixfmt)) < 0) return -1;
    if (lcd_cmd_data(0xB4, invctl, sizeof(invctl)) < 0) return -1;
    if (lcd_cmd_data(0xB6, disfun, sizeof(disfun)) < 0) return -1;
    if (lcd_cmd_data(0xE8, e8, sizeof(e8)) < 0) return -1;
    if (lcd_cmd_data(0xC1, c1, sizeof(c1)) < 0) return -1;
    if (lcd_cmd_data(0xC2, c2, sizeof(c2)) < 0) return -1;
    if (lcd_cmd_data(0xC5, c5, sizeof(c5)) < 0) return -1;
    if (lcd_cmd_data(0xE0, pgamma, sizeof(pgamma)) < 0) return -1;
    if (lcd_cmd_data(0xE1, ngamma, sizeof(ngamma)) < 0) return -1;
    if (lcd_cmd_data(0xF0, f0_3c, sizeof(f0_3c)) < 0) return -1;
    if (lcd_cmd_data(0xF0, f0_69, sizeof(f0_69)) < 0) return -1;

    if (lcd_cmd(0x11) < 0)
        return -1;
    msleep(150);

    if (lcd_cmd(0x20) < 0)
        return -1;

    if (lcd_cmd(0x29) < 0)
        return -1;
    msleep(50);

    return gpio_write(GPIO_LED, 1);
}

static int lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];

    data[0] = x0 >> 8;
    data[1] = x0 & 0xff;
    data[2] = x1 >> 8;
    data[3] = x1 & 0xff;
    if (lcd_cmd_data(0x2A, data, sizeof(data)) < 0)
        return -1;

    data[0] = y0 >> 8;
    data[1] = y0 & 0xff;
    data[2] = y1 >> 8;
    data[3] = y1 & 0xff;
    if (lcd_cmd_data(0x2B, data, sizeof(data)) < 0)
        return -1;

    return lcd_cmd(0x2C);
}

static int lcd_fill_color(uint16_t color)
{
    uint8_t *frame;
    size_t total = (size_t)LCD_WIDTH * LCD_HEIGHT * 2;

    frame = malloc(total);
    if (!frame)
        return -1;

    for (size_t i = 0; i < total; i += 2) {
        frame[i] = color >> 8;
        frame[i + 1] = color & 0xff;
    }

    if (lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1) < 0)
        goto fail;
    if (gpio_write(GPIO_DC, 1) < 0)
        goto fail;
    if (lcd_select(1) < 0)
        goto fail;

    if (spi_write_bytes(frame, total) < 0)
        goto fail;

    free(frame);
    return lcd_select(0);

fail:
    free(frame);
    return -1;
}

static int lcd_fill_rgb666(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *frame;
    size_t total = (size_t)LCD_WIDTH * LCD_HEIGHT * 3;

    frame = malloc(total);
    if (!frame)
        return -1;

    for (size_t i = 0; i < total; i += 3) {
        frame[i] = r;
        frame[i + 1] = g;
        frame[i + 2] = b;
    }

    if (lcd_set_pixfmt(0x66) < 0)
        goto fail;
    if (lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1) < 0)
        goto fail;
    if (gpio_write(GPIO_DC, 1) < 0)
        goto fail;
    if (lcd_select(1) < 0)
        goto fail;

    if (spi_write_bytes(frame, total) < 0)
        goto fail;

    free(frame);
    return lcd_select(0);

fail:
    free(frame);
    return -1;
}

static int lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h, uint16_t color)
{
    uint8_t *chunk;
    size_t total = (size_t)w * h * 2;
    size_t sent = 0;

    if (!w || !h)
        return 0;
    if (x0 + w > LCD_WIDTH || y0 + h > LCD_HEIGHT)
        return -1;

    chunk = malloc(raw_spi_payload);
    if (!chunk)
        return -1;

    for (unsigned int i = 0; i < raw_spi_payload; i += 2) {
        chunk[i] = color >> 8;
        chunk[i + 1] = color & 0xff;
    }

    if (lcd_set_window(x0, y0, x0 + w - 1, y0 + h - 1) < 0)
        goto fail;
    if (gpio_write(GPIO_DC, 1) < 0)
        goto fail;
    if (lcd_select(1) < 0)
        goto fail;

    while (sent < total) {
        size_t n = total - sent;
        if (n > raw_spi_payload)
            n = raw_spi_payload;
        if (spi_write_bytes(chunk, n) < 0)
            goto fail;
        sent += n;
    }

    free(chunk);
    return lcd_select(0);

fail:
    free(chunk);
    return -1;
}

static int lcd_probe_blocks(void)
{
    if (lcd_fill_rect(0, 0, 40, 40, 0xF800) < 0)
        return -1;
    if (lcd_fill_rect(48, 0, 40, 40, 0x07E0) < 0)
        return -1;
    if (lcd_fill_rect(96, 0, 40, 40, 0x001F) < 0)
        return -1;
    if (lcd_fill_rect(144, 0, 40, 40, 0xFFFF) < 0)
        return -1;
    return 0;
}

static int lcd_color_bars(void)
{
    static const uint16_t colors[] = {
        0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF, 0x0000
    };
    uint8_t line[LCD_WIDTH * 2];

    if (lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1) < 0)
        return -1;
    if (gpio_write(GPIO_DC, 1) < 0)
        return -1;
    if (lcd_select(1) < 0)
        return -1;

    for (unsigned int y = 0; y < LCD_HEIGHT; y++) {
        for (unsigned int x = 0; x < LCD_WIDTH; x++) {
            uint16_t color = colors[x / (LCD_WIDTH / 8)];
            line[x * 2] = color >> 8;
            line[x * 2 + 1] = color & 0xff;
        }
        if (spi_write_bytes(line, sizeof(line)) < 0)
            return -1;
    }

    return lcd_select(0);
}

static int lcd_raw_file(const char *path)
{
    FILE *fp;
    uint8_t *frame;
    size_t frame_bytes = (size_t)LCD_WIDTH * LCD_HEIGHT * 2;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "open raw file %s failed: %s\n", path, strerror(errno));
        return -1;
    }

    frame = malloc(frame_bytes);
    if (!frame)
        goto fail;
    if (fread(frame, 1, frame_bytes, fp) != frame_bytes) {
        fprintf(stderr, "short raw file\n");
        goto fail;
    }

    if (lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1) < 0)
        goto fail;
    if (gpio_write(GPIO_DC, 1) < 0)
        goto fail;
    if (lcd_select(1) < 0)
        goto fail;

    for (unsigned int y = 0; y < LCD_HEIGHT; y++) {
        uint8_t *line = frame + (size_t)y * LCD_WIDTH * 2;

        if (spi_write_bytes(line, LCD_WIDTH * 2) < 0)
            goto fail;
    }

    fclose(fp);
    free(frame);
    return lcd_select(0);

fail:
    free(frame);
    fclose(fp);
    return -1;
}

static int read_full_fd(int fd, uint8_t *buf, size_t len)
{
    size_t got = 0;

    while (got < len) {
        ssize_t ret = read(fd, buf + got, len - got);

        if (ret == 0)
            return got ? -1 : 0;
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        got += (size_t)ret;
    }

    return 1;
}

static int lcd_kernel_stdin(const char *dev_path, unsigned int max_frames)
{
    size_t frame_bytes = (size_t)LCD_WIDTH * LCD_HEIGHT * 2;
    uint8_t *frame;
    unsigned int frames = 0;
    double t0;
    int kfd;

    frame = malloc(frame_bytes);
    if (!frame)
        return -1;

    kfd = open(dev_path, O_RDWR);
    if (kfd < 0) {
        fprintf(stderr, "open %s failed: %s\n", dev_path, strerror(errno));
        free(frame);
        return -1;
    }

    t0 = now_sec();
    for (;;) {
        int ret = read_full_fd(STDIN_FILENO, frame, frame_bytes);

        if (ret == 0)
            break;
        if (ret < 0) {
            fprintf(stderr, "short stdin frame after %u frames\n", frames);
            break;
        }

        if (ioctl(kfd, CH34x_LCD_FRAME_WRITE_WINDOW, frame) < 0) {
            fprintf(stderr, "kernel frame ioctl failed after %u frames: %s\n",
                    frames, strerror(errno));
            goto fail;
        }

        frames++;
        if (max_frames && frames >= max_frames)
            break;
    }

    fprintf(stderr, "kernelstdin frames=%u total=%.3fs fps=%.2f\n",
            frames, now_sec() - t0, frames ? frames / (now_sec() - t0) : 0.0);
    close(kfd);
    free(frame);
    return frames ? 0 : -1;

fail:
    close(kfd);
    free(frame);
    return -1;
}

static int spi_init(unsigned int mode, unsigned int clock)
{
    mSpiCfgS cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.iMode = (UCHAR)mode;
    cfg.iClock = (UCHAR)clock;
    cfg.iByteOrder = 1;
    cfg.iSpiOutDefaultData = 0xFF;
    cfg.iChipSelect = 0;
    cfg.CS1Polarity = 0;
    cfg.CS2Polarity = 0;
    cfg.iIsAutoDeativeCS = 0;
    cfg.iActiveDelay = 0;
    cfg.iDelayDeactive = 0;

    if (!CH347SPI_Init(dev, &cfg)) {
        fprintf(stderr, "CH347SPI_Init failed\n");
        return -1;
    }

    if (lcd_select(0) < 0)
        return -1;

    return 0;
}

int main(int argc, char **argv)
{
    unsigned int mode = 0;
    unsigned int clock = 0;
    const char *pattern = "probe";
    double t0, t1;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc > 1)
        mode = (unsigned int)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        clock = (unsigned int)strtoul(argv[2], NULL, 0);
    if (argc > 3)
        pattern = argv[3];
    if (argc > 4)
        raw_spi_payload = (size_t)strtoul(argv[4], NULL, 0);
    if (argc > 5)
        raw_spi_delay_us = (unsigned int)strtoul(argv[5], NULL, 0);
    if (argc > 6)
        transfer_method = argv[6];

    if (raw_spi_payload < 1)
        raw_spi_payload = 1;
    if (raw_spi_payload > SPI_BIG_MAX_PAYLOAD)
        raw_spi_payload = SPI_BIG_MAX_PAYLOAD;
    if (raw_spi_payload & 1)
        raw_spi_payload--;
    if (raw_spi_payload < 2)
        raw_spi_payload = 2;

    printf("Opening CH347 device 0...\n");
    dev = CH347OpenDevice(0);
    if (dev < 0) {
        fprintf(stderr, "CH347OpenDevice failed\n");
        return 1;
    }

    printf("Initializing SPI mode %u clock index %u, payload %zu, delay %uus, method %s...\n",
           mode, clock, raw_spi_payload, raw_spi_delay_us, transfer_method);
    if (spi_init(mode, clock) < 0)
        goto fail;

    gpio_state = GPIO_MASK(GPIO_RESET);
    if (gpio_apply() < 0)
        goto fail;

    printf("Initializing ST7796 and enabling backlight...\n");
    if (lcd_init() < 0)
        goto fail;

    if (!strcmp(pattern, "probe")) {
        printf("Drawing small RGBW probe blocks only.\n");
        t0 = now_sec();
        if (lcd_probe_blocks() < 0)
            goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should show four small blocks: red, green, blue, white.\n", t1 - t0);
    } else if (!strcmp(pattern, "arm")) {
        printf("Arming full-screen RAMWR window and leaving LCD CS active...\n");
        if (lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1) < 0)
            goto fail;
        if (gpio_write(GPIO_DC, 1) < 0)
            goto fail;
        if (lcd_select(1) < 0)
            goto fail;
        printf("Armed. The next raw C4 data stream should be accepted as LCD pixels.\n");
    } else if (!strcmp(pattern, "bars")) {
        printf("Drawing color bars...\n");
        t0 = now_sec();
        if (lcd_color_bars() < 0)
            goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should now show color bars.\n", t1 - t0);
    } else if (!strcmp(pattern, "rawfile")) {
        const char *path = argc > 7 ? argv[7] : "/tmp/gears_static_own565.raw";

        printf("Drawing raw RGB565BE file %s using stable driver path...\n", path);
        t0 = now_sec();
        if (lcd_raw_file(path) < 0)
            goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should now show raw file.\n", t1 - t0);
    } else if (!strcmp(pattern, "kernelstdin")) {
        const char *dev_path = argc > 7 ? argv[7] : "/dev/ch34x_pis0";
        unsigned int max_frames = argc > 8 ? (unsigned int)strtoul(argv[8], NULL, 0) : 0;

        printf("Streaming RGB565BE stdin through kernel fast path dev=%s max_frames=%u...\n",
               dev_path, max_frames);
        if (lcd_kernel_stdin(dev_path, max_frames) < 0)
            goto fail;
    } else if (!strcmp(pattern, "cycle")) {
        printf("Drawing red/green/blue/white screens...\n");
        t0 = now_sec();
        if (lcd_fill_color(0xF800) < 0) goto fail;
        msleep(800);
        if (lcd_fill_color(0x07E0) < 0) goto fail;
        msleep(800);
        if (lcd_fill_color(0x001F) < 0) goto fail;
        msleep(800);
        if (lcd_fill_color(0xFFFF) < 0) goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should now show white.\n", t1 - t0);
    } else if (!strcmp(pattern, "green")) {
        printf("Drawing solid green...\n");
        t0 = now_sec();
        if (lcd_fill_color(0x07E0) < 0) goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should now show solid green.\n", t1 - t0);
    } else if (!strcmp(pattern, "blue")) {
        printf("Drawing solid blue...\n");
        t0 = now_sec();
        if (lcd_fill_color(0x001F) < 0) goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should now show solid blue.\n", t1 - t0);
    } else if (!strcmp(pattern, "blue666")) {
        printf("Drawing solid blue in RGB666 mode...\n");
        t0 = now_sec();
        if (lcd_fill_rgb666(0x00, 0x00, 0xFC) < 0) goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should now show solid blue.\n", t1 - t0);
    } else if (!strcmp(pattern, "green666")) {
        printf("Drawing solid green in RGB666 mode...\n");
        t0 = now_sec();
        if (lcd_fill_rgb666(0x00, 0xFC, 0x00) < 0) goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should now show solid green.\n", t1 - t0);
    } else if (!strcmp(pattern, "red666")) {
        printf("Drawing solid red in RGB666 mode...\n");
        t0 = now_sec();
        if (lcd_fill_rgb666(0xFC, 0x00, 0x00) < 0) goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should now show solid red.\n", t1 - t0);
    } else if (!strcmp(pattern, "white")) {
        printf("Drawing solid white...\n");
        t0 = now_sec();
        if (lcd_fill_color(0xFFFF) < 0) goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should now show solid white.\n", t1 - t0);
    } else if (!strcmp(pattern, "black")) {
        printf("Drawing solid black...\n");
        t0 = now_sec();
        if (lcd_fill_color(0x0000) < 0) goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should now show solid black.\n", t1 - t0);
    } else {
        printf("Drawing solid red...\n");
        t0 = now_sec();
        if (lcd_fill_color(0xF800) < 0) goto fail;
        t1 = now_sec();
        printf("Done in %.3fs. LCD should now show solid red.\n", t1 - t0);
    }
    CH347CloseDevice(dev);
    return 0;

fail:
    CH347CloseDevice(dev);
    return 1;
}
