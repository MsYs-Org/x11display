# Development Notes

## Timeline Summary

1. We first got ST7796 initialization working through CH347 using the vendor
   library path and confirmed solid color fills.
2. We moved to raw CH347 USB bulk packets (`0xC4`) because the normal library
   write path had too much call overhead for LCD streaming.
3. Full-screen X11 capture worked with:
   `Xvfb -> ffmpeg x11grab -> rgb565be -> ch347_video_sink`.
4. Dirty rendering was added with a shadow framebuffer, tile comparison, dirty
   tile merge, and a full-screen fallback.
5. Async dirty calculation was tried, but it caused freezes and silent lockups.
   It has been removed.
6. Multi-window dirty output was tried. It is valid for ST7796, but on this
   Linux -> USB -> CH347 -> SPI path the extra window commands were too costly.
7. The stable X11 path became one dirty bounding box, plus a full refresh when
   dirty area is too large.
8. Video tests showed the full-screen raw link can be faster than dirty X11, but
   RAMWR pointer drift can shift pixels unless RAMWR is reissued regularly.
9. An earlier standalone X11 path started `openbox` for title bars. The MSYS
   path now defaults to `WM=none`; Openbox is an explicit standalone option.
10. XPT2046 touch support was added on the shared SPI bus using SCS1 and GPIO3,
    but it is disabled by default until the hardware is connected.
11. LCD-side cursor overlay and a 5-point calibration mode were added for the
    XPT2046 path.

## Dirty Rendering Model

The LCD is not the render target. The render target is a RAM framebuffer.

```
incoming frame
    |
    v
current framebuffer
    |
    v
compare against previous framebuffer by tiles
    |
    v
dirty tile map
    |
    v
rect list / bbox / full-screen fallback
    |
    v
LCD address window + RGB565 data
```

Current tile size:

```c
TILE_W = 32
TILE_H = 15
```

That makes one tile exactly `32 * 15 * 2 = 960` bytes, or two 480-byte CH347 SPI
payloads.

## Why Not Many Rectangles

ST7796 supports multiple windows:

```
CASET, RASET, RAMWR, pixels for rect A
CASET, RASET, RAMWR, pixels for rect B
```

But CH347 makes every command/DC transition expensive because it is another USB
transaction and often another GPIO write/ack. In testing, `CH347_MAX_RECTS=8`
made output fall to about 3 FPS and looked stuck. `CH347_MAX_RECTS=1` was much
better for this link.

## Why Not SPI Row Skips

Inside one RAMWR window, ST7796 writes pixels linearly:

```
x++, wrap to next y when x reaches the window end
```

There is no skip command in the SPI pixel stream. If two dirty rows have a clean
row between them, either split into two windows or send the clean row too as
part of a larger rectangle.

## Current Stability Rules

- Use `CH347_CLOCK=1` (30 MHz) as the default. 60 MHz can be faster but is more
  likely to show corruption on the current wiring.
- Keep `CH347_MAX_RECTS=1` for X11 dirty.
- Keep async dirty disabled; the code no longer has that feature.
- Keep `CH347_TOUCH=0` until XPT2046 is wired. Touch polling is cooperative and
  runs between displayed frames, not in a separate SPI thread.
- Use `scripts/calibrate_touch.sh` after wiring. It writes
  `ch347/touch_calibration.env`, which can be sourced before normal startup.
- For video/raw full-screen playback, use `CH347_RAMWR_EACH_FRAME=1` if the
  image shifts horizontally over time.
- Do not run aggressive tests while SSH/network stability matters. The start and
  stop scripts only touch their own Xvfb, app, ffmpeg, sink, and CH347 interface.

## Observed Speeds

Approximate results on the current OpenStick / Qualcomm 410 setup:

- X11 dirty bbox path: around 9 to 10 output FPS with glxgears.
- Multi-window dirty: around 3 FPS and visually poor.
- Full-screen raw video continuous: around 15 FPS, but may drift.
- Full-screen raw video with `RAMWR` each frame: around 9 FPS, more stable.

## Files That Matter

- `src/ch347_dirty_usb_sink.c`: X11 dirty renderer sink.
- `src/ch347_video_sink.c`: full-screen RGB565 raw sink.
- `src/ch347_st7796_test.c`: LCD init and full-screen RAMWR arm tool.
- `scripts/start_ch347_dirty_usb_x11.sh`: user entry point for X11 capture.
- `scripts/ch347_dirty_usb_x11_daemon.sh`: owns Xvfb/app/ffmpeg/CH347 loop.
- `driver/ch347_fast_kernel/ch34x_pis.c`: patched CH347 kernel driver source.
- `docs/X11_TOUCH.md`: X11 display, title-bar/window-manager, and touch notes.

## 2026-07-10 capture latency fix

The XDamage path no longer sends 307200-byte frames through an anonymous pipe.
It uses a three-slot mmap mailbox (`$RUN_DIR/frame.mailbox`) with atomic sequence
publication. Capture always overwrites the newest slot and the USB sink consumes
the newest complete frame, so stale frames cannot queue during a low-to-high
activity transition.

Application SIGSTOP/SIGCONT gating now defaults off. XDamage already provides
idle behavior without stopping applications; gating caused delayed repaint and
bursty recovery. The LCD debug labels are `C` (capture/source FPS) and `P`
(actual dirty frames pushed), which are expected to differ when frames are
coalesced or dropped to preserve low latency.
