#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xdamage.h>

#include "frame_mailbox.h"
#include "frame_rotation.h"

static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t reload_requested;
static volatile sig_atomic_t force_refresh_requested;
static struct frame_mailbox_header *signal_mailbox;

struct mask_info {
    unsigned long mask;
    unsigned int shift;
    unsigned int bits;
};

struct rect {
    unsigned int x;
    unsigned int y;
    unsigned int w;
    unsigned int h;
};

static void handle_signal(int sig)
{
    if (sig != SIGUSR1 && sig != SIGUSR2 && signal_mailbox)
        atomic_store_explicit(&signal_mailbox->producer_alive, 0,
                memory_order_release);
    if (sig == SIGUSR1)
        reload_requested = 1;
    else if (sig == SIGUSR2)
        force_refresh_requested = 1;
    else
        stop_requested = 1;
}

static double now_sec(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static uint64_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static unsigned int env_u32(const char *name, unsigned int def)
{
    const char *v = getenv(name);

    if (!v || !*v)
        return def;
    return (unsigned int)strtoul(v, NULL, 0);
}

static double env_double(const char *name, double def)
{
    const char *v = getenv(name);

    if (!v || !*v)
        return def;
    return atof(v);
}

static void read_fps_file(const char *path, double current_max_fps,
        double current_idle_fps, double *max_fps_out, double *idle_fps_out)
{
    char line[128];
    FILE *fp;
    double max_fps = current_max_fps;
    double idle_fps = current_idle_fps;

    *max_fps_out = current_max_fps;
    *idle_fps_out = current_idle_fps;
    if (!path || !*path)
        return;
    fp = fopen(path, "r");
    if (!fp)
        return;
    while (fgets(line, sizeof(line), fp)) {
        double value;

        if (sscanf(line, "XCAP_MAX_FPS=%lf", &value) == 1 ||
                sscanf(line, "FPS=%lf", &value) == 1) {
            if (value > 0.0)
                max_fps = value;
        } else if (sscanf(line, "XCAP_IDLE_FPS=%lf", &value) == 1) {
            if (value >= 0.0)
                idle_fps = value;
        }
    }
    fclose(fp);
    *max_fps_out = max_fps;
    *idle_fps_out = idle_fps;
}

static int read_rotation_file(const char *path,
        enum frame_rotation current_rotation,
        enum frame_rotation *rotation_out)
{
    FILE *stream;
    char line[192];
    enum frame_rotation selected = current_rotation;
    int found = 0;

    if (!rotation_out)
        return 0;
    *rotation_out = current_rotation;
    if (!path || !*path)
        return 1;
    stream = fopen(path, "r");
    if (!stream)
        return 0;
    while (fgets(line, sizeof(line), stream)) {
        char value[64];
        char extra;
        char *cursor = line;

        while (*cursor == ' ' || *cursor == '\t')
            cursor++;
        if (!*cursor || *cursor == '\n' || *cursor == '#')
            continue;
        if (sscanf(cursor, "CH347_DISPLAY_ROTATION=%63[^\r\n]%c", value,
                    &extra) < 1)
            continue;
        if (found || !frame_rotation_parse(value, &selected)) {
            fclose(stream);
            return 0;
        }
        found = 1;
    }
    if (ferror(stream) || !found) {
        fclose(stream);
        return 0;
    }
    fclose(stream);
    *rotation_out = selected;
    return 1;
}

static void init_mask(struct mask_info *mi, unsigned long mask)
{
    mi->mask = mask;
    mi->shift = 0;
    mi->bits = 0;

    if (!mask)
        return;
    while (((mask >> mi->shift) & 1u) == 0)
        mi->shift++;
    while (((mask >> (mi->shift + mi->bits)) & 1u) != 0)
        mi->bits++;
}

static uint8_t scale_component(unsigned long pixel, const struct mask_info *mi)
{
    unsigned long v;
    unsigned long max;

    if (!mi->mask || !mi->bits)
        return 0;
    v = (pixel & mi->mask) >> mi->shift;
    max = (1ul << mi->bits) - 1;
    if (max == 255)
        return (uint8_t)v;
    return (uint8_t)((v * 255 + max / 2) / max);
}

static unsigned long read_pixel(const XImage *img, const char *p, int bytes)
{
    unsigned long v = 0;

    if (img->byte_order == LSBFirst) {
        for (int i = 0; i < bytes; i++)
            v |= (unsigned long)(uint8_t)p[i] << (8 * i);
    } else {
        for (int i = 0; i < bytes; i++)
            v = (v << 8) | (uint8_t)p[i];
    }

    return v;
}

static void convert_to_rgb565be(const XImage *img, uint8_t *out,
        const struct mask_info *rm, const struct mask_info *gm,
        const struct mask_info *bm)
{
    int bytes = img->bits_per_pixel / 8;

    if (img->bits_per_pixel == 32 && img->byte_order == LSBFirst &&
            img->red_mask == 0xff0000 && img->green_mask == 0x00ff00 &&
            img->blue_mask == 0x0000ff) {
        for (int y = 0; y < img->height; y++) {
            const uint8_t *row = (const uint8_t *)img->data +
                (size_t)y * img->bytes_per_line;
            uint8_t *dst = out + (size_t)y * img->width * 2;

            for (int x = 0; x < img->width; x++) {
                uint8_t b = row[(size_t)x * 4 + 0];
                uint8_t g = row[(size_t)x * 4 + 1];
                uint8_t r = row[(size_t)x * 4 + 2];
                uint16_t rgb = (uint16_t)(((r & 0xf8) << 8) |
                        ((g & 0xfc) << 3) | (b >> 3));

                dst[(size_t)x * 2 + 0] = (uint8_t)(rgb >> 8);
                dst[(size_t)x * 2 + 1] = (uint8_t)(rgb & 0xff);
            }
        }
        return;
    }

    for (int y = 0; y < img->height; y++) {
        const char *row = img->data + (size_t)y * img->bytes_per_line;

        for (int x = 0; x < img->width; x++) {
            unsigned long pixel = read_pixel(img, row + (size_t)x * bytes,
                    bytes);
            uint8_t r = scale_component(pixel, rm);
            uint8_t g = scale_component(pixel, gm);
            uint8_t b = scale_component(pixel, bm);
            uint16_t rgb = (uint16_t)(((r >> 3) << 11) |
                    ((g >> 2) << 5) | (b >> 3));
            size_t off = ((size_t)y * img->width + x) * 2;

            out[off] = (uint8_t)(rgb >> 8);
            out[off + 1] = (uint8_t)(rgb & 0xff);
        }
    }
}

static void put_le16(uint8_t *p, unsigned int v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void put_le32(uint8_t *p, unsigned int v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static int write_rect_packet(unsigned int screen_w, unsigned int screen_h,
        unsigned int seq, const struct rect *r, const uint8_t *pixels)
{
    uint8_t hdr[16];
    uint8_t rh[12];
    unsigned int count = r ? 1 : 0;
    unsigned int len = r ? r->w * r->h * 2 : 0;

    memcpy(hdr, "XDR1", 4);
    put_le16(hdr + 4, screen_w);
    put_le16(hdr + 6, screen_h);
    put_le16(hdr + 8, count);
    put_le16(hdr + 10, 0);
    put_le32(hdr + 12, seq);
    if (fwrite(hdr, 1, sizeof(hdr), stdout) != sizeof(hdr))
        return -1;

    if (!r)
        return fflush(stdout);

    put_le16(rh + 0, r->x);
    put_le16(rh + 2, r->y);
    put_le16(rh + 4, r->w);
    put_le16(rh + 6, r->h);
    put_le32(rh + 8, len);
    if (fwrite(rh, 1, sizeof(rh), stdout) != sizeof(rh))
        return -1;
    if (fwrite(pixels, 1, len, stdout) != len)
        return -1;
    return fflush(stdout);
}

static int clip_rect(struct rect *r, unsigned int screen_w,
        unsigned int screen_h)
{
    if (!r->w || !r->h || r->x >= screen_w || r->y >= screen_h)
        return 0;
    if (r->x + r->w > screen_w)
        r->w = screen_w - r->x;
    if (r->y + r->h > screen_h)
        r->h = screen_h - r->y;
    return r->w && r->h;
}

static void merge_rect(struct rect *dst, int *have, const struct rect *src)
{
    unsigned int x0;
    unsigned int y0;
    unsigned int x1;
    unsigned int y1;

    if (!*have) {
        *dst = *src;
        *have = 1;
        return;
    }

    x0 = dst->x < src->x ? dst->x : src->x;
    y0 = dst->y < src->y ? dst->y : src->y;
    x1 = dst->x + dst->w > src->x + src->w ?
        dst->x + dst->w : src->x + src->w;
    y1 = dst->y + dst->h > src->y + src->h ?
        dst->y + dst->h : src->y + src->h;
    dst->x = x0;
    dst->y = y0;
    dst->w = x1 - x0;
    dst->h = y1 - y0;
}

static XImage *create_shm_image(Display *dpy, Visual *visual, int depth,
        unsigned int width, unsigned int height, XShmSegmentInfo *shm)
{
    XImage *img;
    size_t size;

    memset(shm, 0, sizeof(*shm));
    img = XShmCreateImage(dpy, visual, (unsigned int)depth, ZPixmap, NULL,
            shm, width, height);
    if (!img)
        return NULL;

    size = (size_t)img->bytes_per_line * img->height;
    shm->shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
    if (shm->shmid < 0) {
        XDestroyImage(img);
        return NULL;
    }

    shm->shmaddr = shmat(shm->shmid, NULL, 0);
    if (shm->shmaddr == (char *)-1) {
        shmctl(shm->shmid, IPC_RMID, NULL);
        XDestroyImage(img);
        return NULL;
    }

    shm->readOnly = False;
    img->data = shm->shmaddr;
    if (!XShmAttach(dpy, shm)) {
        shmdt(shm->shmaddr);
        shmctl(shm->shmid, IPC_RMID, NULL);
        XDestroyImage(img);
        return NULL;
    }
    XSync(dpy, False);
    shmctl(shm->shmid, IPC_RMID, NULL);
    return img;
}

static XImage *create_capture_image(Display *dpy, Visual *visual, int depth,
        unsigned int width, unsigned int height, int *use_shm,
        XShmSegmentInfo *shm)
{
    XImage *image = NULL;

    if (*use_shm)
        image = create_shm_image(dpy, visual, depth, width, height, shm);
    if (image)
        return image;
    *use_shm = 0;
    memset(shm, 0, sizeof(*shm));
    return XCreateImage(dpy, visual, (unsigned int)depth, ZPixmap, 0, NULL,
            width, height, 32, 0);
}

static void destroy_capture_image(Display *dpy, XImage *image, int use_shm,
        XShmSegmentInfo *shm)
{
    if (!image)
        return;
    if (use_shm) {
        XShmDetach(dpy, shm);
        /* The server must consume Detach before this client removes its last
         * local mapping.  This is especially important during live RandR
         * rotation, where another segment is attached immediately after. */
        XSync(dpy, False);
        XDestroyImage(image);
        shmdt(shm->shmaddr);
    } else {
        XDestroyImage(image);
    }
}

static int capture_frame(Display *dpy, Drawable root, XImage **fallback_img,
        XImage *shm_img, int use_shm, uint8_t *out,
        const struct mask_info *rm, const struct mask_info *gm,
        const struct mask_info *bm)
{
    XImage *img = shm_img;

    if (use_shm) {
        if (!XShmGetImage(dpy, root, shm_img, 0, 0, AllPlanes))
            return -1;
        XSync(dpy, False);
    } else {
        if (*fallback_img)
            XDestroyImage(*fallback_img);
        *fallback_img = XGetImage(dpy, root, 0, 0,
                (unsigned int)shm_img->width, (unsigned int)shm_img->height,
                AllPlanes, ZPixmap);
        if (!*fallback_img)
            return -1;
        img = *fallback_img;
    }

    convert_to_rgb565be(img, out, rm, gm, bm);
    return 0;
}

static int capture_rect_packet(Display *dpy, Drawable root,
        XImage **fallback_img, XImage *shm_img, int use_shm, uint8_t *out,
        const struct mask_info *rm, const struct mask_info *gm,
        const struct mask_info *bm, unsigned int screen_w,
        unsigned int screen_h, unsigned int seq, const struct rect *r)
{
    XImage *img;
    int full = r->x == 0 && r->y == 0 && r->w == screen_w &&
        r->h == screen_h;

    if (full && use_shm) {
        if (!XShmGetImage(dpy, root, shm_img, 0, 0, AllPlanes))
            return -1;
        XSync(dpy, False);
        img = shm_img;
    } else {
        if (*fallback_img)
            XDestroyImage(*fallback_img);
        *fallback_img = XGetImage(dpy, root, (int)r->x, (int)r->y,
                r->w, r->h, AllPlanes, ZPixmap);
        if (!*fallback_img)
            return -1;
        img = *fallback_img;
    }

    convert_to_rgb565be(img, out, rm, gm, bm);
    return write_rect_packet(screen_w, screen_h, seq, r, out);
}

static int mailbox_open(const char *path, unsigned int width, unsigned int height,
        size_t frame_bytes, void **mapping_out, size_t *mapping_size_out)
{
    struct frame_mailbox_header *header;
    size_t mapping_size = frame_mailbox_size(frame_bytes);
    void *mapping;
    int fd;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0 || ftruncate(fd, (off_t)mapping_size) < 0) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED)
        return -1;
    memset(mapping, 0, FRAME_MAILBOX_HEADER_BYTES);
    header = mapping;
    header->magic = FRAME_MAILBOX_MAGIC;
    header->version = FRAME_MAILBOX_VERSION;
    header->width = width;
    header->height = height;
    header->frame_bytes = (uint32_t)frame_bytes;
    header->slot_count = FRAME_MAILBOX_SLOTS;
    atomic_store_explicit(&header->published_seq, 0, memory_order_release);
    atomic_store_explicit(&header->consumed_seq, 0, memory_order_release);
    atomic_store_explicit(&header->producer_alive, 1, memory_order_release);
    atomic_store_explicit(&header->producer_heartbeat_ms, now_ms(),
            memory_order_release);
    *mapping_out = mapping;
    *mapping_size_out = mapping_size;
    return 0;
}

static void mailbox_publish(void *mapping, size_t frame_bytes,
        const uint8_t *frame, uint64_t seq)
{
    struct frame_mailbox_header *header = mapping;
    unsigned int slot = (unsigned int)(seq % FRAME_MAILBOX_SLOTS);

    atomic_store_explicit(&header->slot_seq[slot], 0, memory_order_relaxed);
    memcpy(frame_mailbox_slot(mapping, frame_bytes, slot), frame, frame_bytes);
    atomic_store_explicit(&header->slot_seq[slot], seq, memory_order_release);
    atomic_store_explicit(&header->published_seq, seq, memory_order_release);
}

int main(int argc, char **argv)
{
    const char *display_name = argc > 1 ? argv[1] : getenv("DISPLAY");
    unsigned int width = argc > 2 ? (unsigned int)strtoul(argv[2], NULL, 0) :
        env_u32("WIDTH", 320);
    unsigned int height = argc > 3 ? (unsigned int)strtoul(argv[3], NULL, 0) :
        env_u32("HEIGHT", 480);
    double max_fps = argc > 4 ? atof(argv[4]) : env_double("XCAP_MAX_FPS", 30.0);
    double idle_fps = argc > 5 ? atof(argv[5]) : env_double("XCAP_IDLE_FPS", 1.0);
    unsigned int max_frames = env_u32("MAX_FRAMES", 0);
    int debug = env_u32("XCAP_DEBUG", 0) != 0;
    const char *output_mode = getenv("XCAP_OUTPUT");
    const char *mailbox_path = getenv("XCAP_MAILBOX");
    const char *fps_file = getenv("XCAP_FPS_FILE");
    const char *rotation_file = getenv("XCAP_ROTATION_FILE");
    const char *rotation_text = getenv("XCAP_ROTATION");
    enum frame_rotation rotation;
    int output_rects = !output_mode || !strcmp(output_mode, "rects");
    Display *dpy;
    int screen;
    Window root;
    Visual *visual;
    int depth;
    int damage_event = 0;
    int damage_error = 0;
    int have_damage;
    Damage damage = 0;
    int use_shm;
    XShmSegmentInfo *shm;
    XImage *img;
    XImage *fallback_img = NULL;
    uint8_t *out;
    uint8_t *capture_buffer;
    uint8_t *last_frame = NULL;
    int have_last_frame = 0;
    struct mask_info rm;
    struct mask_info gm;
    struct mask_info bm;
    double min_interval;
    double idle_interval;
    double last_control_poll = -1.0e9;
    double last_out = -1.0e9;
    unsigned int frames = 0;
    uint64_t published_frames = 0;
    int dirty = 1;
    int have_pending = 0;
    struct rect pending = {0, 0, 0, 0};
    int xfd;
    void *mailbox = NULL;
    size_t mailbox_size = 0;
    size_t frame_bytes = (size_t)width * height * 2;
    unsigned int output_width;
    unsigned int output_height;

    if (!display_name || !*display_name)
        display_name = ":24";
    if (!width || !height) {
        fprintf(stderr, "xdamage_capture invalid size %ux%u\n", width, height);
        return 1;
    }
    if (!frame_rotation_parse(rotation_text, &rotation)) {
        fprintf(stderr, "xdamage_capture invalid rotation %s\n",
                rotation_text ? rotation_text : "");
        return 1;
    }
    frame_rotation_output_size(rotation, width, height, &output_width,
            &output_height);
    if (output_rects && rotation != FRAME_ROTATION_NORMAL) {
        fprintf(stderr,
                "xdamage_capture rotated output requires XCAP_OUTPUT=frame\n");
        return 1;
    }
    if (max_fps <= 0.0)
        max_fps = 30.0;
    if (idle_fps < 0.0)
        idle_fps = 0.0;
    min_interval = 1.0 / max_fps;
    idle_interval = idle_fps > 0.0 ? 1.0 / idle_fps : 0.0;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, handle_signal);
    signal(SIGUSR1, handle_signal);
    signal(SIGUSR2, handle_signal);

    dpy = XOpenDisplay(display_name);
    if (!dpy) {
        fprintf(stderr, "xdamage_capture cannot open display %s\n",
                display_name);
        return 1;
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    depth = DefaultDepth(dpy, screen);
    have_damage = XDamageQueryExtension(dpy, &damage_event, &damage_error);
    if (have_damage) {
        damage = XDamageCreate(dpy, root, XDamageReportNonEmpty);
    } else {
        /* Structural events are only a compatibility fallback.  Property
         * notifications are not pixels and can be generated continuously by
         * layout policy, so they must never turn into a full-screen capture. */
        XSelectInput(dpy, root, SubstructureNotifyMask | StructureNotifyMask);
    }

    shm = calloc(1, sizeof(*shm));
    if (!shm) {
        fprintf(stderr, "xdamage_capture alloc SHM descriptor failed\n");
        XCloseDisplay(dpy);
        return 1;
    }
    use_shm = XShmQueryExtension(dpy);
    img = create_capture_image(dpy, visual, depth, width, height, &use_shm,
            shm);
    if (!img) {
        fprintf(stderr, "xdamage_capture cannot create XImage\n");
        free(shm);
        XCloseDisplay(dpy);
        return 1;
    }

    out = malloc(frame_bytes);
    if (!out) {
        fprintf(stderr, "xdamage_capture alloc failed\n");
        XCloseDisplay(dpy);
        return 1;
    }
    capture_buffer = rotation == FRAME_ROTATION_NORMAL ? out :
        malloc(frame_bytes);
    if (!capture_buffer) {
        fprintf(stderr, "xdamage_capture rotation buffer alloc failed\n");
        free(out);
        XCloseDisplay(dpy);
        return 1;
    }
    if (!output_rects) {
        last_frame = malloc(frame_bytes);
        if (!last_frame) {
            fprintf(stderr, "xdamage_capture previous-frame alloc failed\n");
            if (capture_buffer != out)
                free(capture_buffer);
            free(out);
            XCloseDisplay(dpy);
            return 1;
        }
    }

    if (mailbox_path && *mailbox_path &&
            mailbox_open(mailbox_path, output_width, output_height, frame_bytes,
                &mailbox, &mailbox_size) < 0) {
        fprintf(stderr, "xdamage_capture mailbox %s failed: %s\n",
                mailbox_path, strerror(errno));
        if (capture_buffer != out)
            free(capture_buffer);
        free(out);
        free(last_frame);
        XCloseDisplay(dpy);
        return 1;
    }
    if (mailbox)
        signal_mailbox = mailbox;

    init_mask(&rm, img->red_mask);
    init_mask(&gm, img->green_mask);
    init_mask(&bm, img->blue_mask);
    xfd = ConnectionNumber(dpy);
    if (debug) {
        fprintf(stderr,
                "xdamage_capture display=%s input=%ux%u output_size=%ux%u rotation=%s depth=%d bpp=%d shm=%d damage=%d output=%s max_fps=%.1f idle_fps=%.1f masks=%lx/%lx/%lx\n",
                display_name, width, height, output_width, output_height,
                frame_rotation_name(rotation), depth, img->bits_per_pixel,
                use_shm, have_damage, output_rects ? "rects" : "frame",
                max_fps, idle_fps, img->red_mask, img->green_mask,
                img->blue_mask);
    }

    while (!stop_requested && (!max_frames || frames < max_frames)) {
        if (mailbox) {
            struct frame_mailbox_header *header = mailbox;

            atomic_store_explicit(&header->producer_heartbeat_ms, now_ms(),
                    memory_order_release);
        }
        double now;
        double wait_s = 1.0;
        int should_capture;

        while (XPending(dpy)) {
            XEvent ev;

            XNextEvent(dpy, &ev);
            if (have_damage && ev.type == damage_event + XDamageNotify) {
                XDamageNotifyEvent *de = (XDamageNotifyEvent *)&ev;
                struct rect r = {
                    (unsigned int)(de->area.x < 0 ? 0 : de->area.x),
                    (unsigned int)(de->area.y < 0 ? 0 : de->area.y),
                    (unsigned int)de->area.width,
                    (unsigned int)de->area.height
                };

                dirty = 1;
                if (clip_rect(&r, width, height))
                    merge_rect(&pending, &have_pending, &r);
                else {
                    r.x = 0;
                    r.y = 0;
                    r.w = width;
                    r.h = height;
                    merge_rect(&pending, &have_pending, &r);
                }
                XDamageSubtract(dpy, damage, None, None);
            } else if (!have_damage && (ev.type == MapNotify ||
                    ev.type == UnmapNotify || ev.type == ConfigureNotify)) {
                struct rect full = {0, 0, width, height};

                dirty = 1;
                merge_rect(&pending, &have_pending, &full);
            }
        }

        now = now_sec();
        if (reload_requested || now - last_control_poll >= 0.25) {
            double new_max_fps;
            double new_idle_fps;
            enum frame_rotation new_rotation = rotation;
            int explicit_reload = reload_requested != 0;

            read_fps_file(fps_file, max_fps, idle_fps, &new_max_fps,
                    &new_idle_fps);
            if (explicit_reload &&
                    !read_rotation_file(rotation_file, rotation,
                        &new_rotation)) {
                fprintf(stderr,
                        "xdamage_capture rotation reload rejected invalid config\n");
                new_rotation = rotation;
            }

            last_control_poll = now;
            reload_requested = 0;
            if (new_max_fps != max_fps) {
                max_fps = new_max_fps;
                min_interval = 1.0 / max_fps;
                if (debug)
                    fprintf(stderr, "xdamage_capture max_fps=%.1f (live)\n",
                            max_fps);
            }
            if (new_idle_fps != idle_fps) {
                idle_fps = new_idle_fps;
                idle_interval = idle_fps > 0.0 ? 1.0 / idle_fps : 0.0;
                if (debug)
                    fprintf(stderr, "xdamage_capture idle_fps=%.1f (live)\n",
                            idle_fps);
            }
            if (new_rotation != rotation) {
                unsigned int new_width;
                unsigned int new_height;
                unsigned int checked_output_width;
                unsigned int checked_output_height;
                XWindowAttributes root_attributes;
                int new_use_shm = XShmQueryExtension(dpy);
                XShmSegmentInfo *new_shm;
                XImage *new_image;
                uint8_t *new_capture_buffer;

                if (new_rotation == FRAME_ROTATION_RIGHT ||
                        new_rotation == FRAME_ROTATION_LEFT) {
                    new_width = output_height;
                    new_height = output_width;
                } else {
                    new_width = output_width;
                    new_height = output_height;
                }
                frame_rotation_output_size(new_rotation, new_width,
                        new_height, &checked_output_width,
                        &checked_output_height);
                if (checked_output_width != output_width ||
                        checked_output_height != output_height ||
                        !XGetWindowAttributes(dpy, root, &root_attributes) ||
                        root_attributes.width != (int)new_width ||
                        root_attributes.height != (int)new_height) {
                    fprintf(stderr,
                            "xdamage_capture rotation reload deferred rotation=%s expected_root=%ux%u\n",
                            frame_rotation_name(new_rotation), new_width,
                            new_height);
                } else if (output_rects &&
                        new_rotation != FRAME_ROTATION_NORMAL) {
                    fprintf(stderr,
                            "xdamage_capture rotation reload requires frame output\n");
                } else {
                    new_shm = calloc(1, sizeof(*new_shm));
                    new_image = new_shm ? create_capture_image(dpy, visual,
                            depth, new_width, new_height, &new_use_shm,
                            new_shm) : NULL;
                    new_capture_buffer =
                        new_rotation == FRAME_ROTATION_NORMAL ? out :
                        malloc(frame_bytes);
                    if (!new_image || !new_capture_buffer) {
                        if (new_image)
                            destroy_capture_image(dpy, new_image, new_use_shm,
                                    new_shm);
                        free(new_shm);
                        if (new_capture_buffer && new_capture_buffer != out)
                            free(new_capture_buffer);
                        fprintf(stderr,
                                "xdamage_capture rotation reload allocation failed\n");
                    } else {
                        if (fallback_img) {
                            XDestroyImage(fallback_img);
                            fallback_img = NULL;
                        }
                        destroy_capture_image(dpy, img, use_shm, shm);
                        free(shm);
                        if (capture_buffer != out)
                            free(capture_buffer);
                        img = new_image;
                        shm = new_shm;
                        use_shm = new_use_shm;
                        capture_buffer = new_capture_buffer;
                        width = new_width;
                        height = new_height;
                        rotation = new_rotation;
                        init_mask(&rm, img->red_mask);
                        init_mask(&gm, img->green_mask);
                        init_mask(&bm, img->blue_mask);
                        dirty = 1;
                        have_pending = 1;
                        pending.x = 0;
                        pending.y = 0;
                        pending.w = width;
                        pending.h = height;
                        force_refresh_requested = 1;
                        fprintf(stderr,
                                "xdamage_capture rotation=%s input=%ux%u output=%ux%u shm=%d (live)\n",
                                frame_rotation_name(rotation), width, height,
                                output_width, output_height, use_shm);
                    }
                }
            }
        }

        if (mailbox) {
            struct frame_mailbox_header *header = mailbox;
            uint64_t published = atomic_load_explicit(&header->published_seq,
                    memory_order_acquire);
            uint64_t consumed = atomic_load_explicit(&header->consumed_seq,
                    memory_order_acquire);

            /* Keep one frame ready while USB is busy, but never build a stale queue. */
            if (published > consumed + 1) {
                wait_s = 0.002;
                goto wait_for_work;
            }
        }

        /* The initial frame is unconditional; all later work is driven by
         * real XDamage.  SIGUSR2 is an explicit panel-recovery request, not
         * a periodic full-frame heartbeat. */
        should_capture = frames == 0 || force_refresh_requested ||
            (dirty && have_pending && now - last_out >= min_interval) ||
            (idle_interval > 0.0 && now - last_out >= idle_interval);
        if (should_capture) {
            if (output_rects) {
                struct rect full = {0, 0, width, height};

                if (frames == 0 || force_refresh_requested) {
                    if (capture_rect_packet(dpy, root, &fallback_img, img,
                                use_shm, out, &rm, &gm, &bm, width, height,
                                frames + 1, &full) < 0)
                        break;
                } else if (have_pending) {
                    if (capture_rect_packet(dpy, root, &fallback_img, img,
                                use_shm, out, &rm, &gm, &bm, width, height,
                                frames + 1, &pending) < 0)
                        break;
                    have_pending = 0;
                    memset(&pending, 0, sizeof(pending));
                } else if (write_rect_packet(width, height, frames + 1, NULL,
                            NULL) < 0) {
                    break;
                }
            } else {
                int frame_changed;

                if (capture_frame(dpy, root, &fallback_img, img, use_shm,
                            capture_buffer,
                            &rm, &gm, &bm) < 0)
                    break;
                if (rotation != FRAME_ROTATION_NORMAL)
                    frame_rotate_rgb565be(capture_buffer, width, height, out,
                            rotation);
                frame_changed = force_refresh_requested || !have_last_frame ||
                    memcmp(last_frame, out, frame_bytes) != 0;
                if (frame_changed) {
                    if (mailbox)
                        mailbox_publish(mailbox, frame_bytes, out,
                                ++published_frames);
                    else if (fwrite(out, 1, frame_bytes, stdout) != frame_bytes ||
                            fflush(stdout) != 0)
                        break;
                    memcpy(last_frame, out, frame_bytes);
                    have_last_frame = 1;
                }
            }
            last_out = now;
            dirty = 0;
            have_pending = 0;
            memset(&pending, 0, sizeof(pending));
            force_refresh_requested = 0;
            frames++;
            continue;
        }

        if (dirty)
            wait_s = min_interval - (now - last_out);
        if (idle_interval > 0.0) {
            double idle_wait = idle_interval - (now - last_out);

            if (idle_wait < wait_s)
                wait_s = idle_wait;
        }
        if (wait_s < 0.0)
            wait_s = 0.0;
        if (fps_file && *fps_file && wait_s > 0.25)
            wait_s = 0.25;

wait_for_work:
        ;

        fd_set fds;
        struct timeval tv;

        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        tv.tv_sec = (time_t)wait_s;
        tv.tv_usec = (suseconds_t)((wait_s - tv.tv_sec) * 1000000.0);
        if (select(xfd + 1, &fds, NULL, NULL, &tv) < 0 && errno != EINTR)
            break;
    }

    if (debug)
        fprintf(stderr, "xdamage_capture frames=%u\n", frames);

    if (damage)
        XDamageDestroy(dpy, damage);
    if (fallback_img)
        XDestroyImage(fallback_img);
    destroy_capture_image(dpy, img, use_shm, shm);
    free(shm);
    if (mailbox) {
        struct frame_mailbox_header *header = mailbox;

        atomic_store_explicit(&header->producer_alive, 0,
                memory_order_release);
        signal_mailbox = NULL;
        munmap(mailbox, mailbox_size);
    }
    if (capture_buffer != out)
        free(capture_buffer);
    free(out);
    free(last_frame);
    XCloseDisplay(dpy);
    return frames ? 0 : 1;
}
