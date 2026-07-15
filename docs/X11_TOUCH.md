# X11, Window Manager, And Touch

## X11 Space

The start script creates an independent virtual X11 display:

```bash
DISPLAY_ID=:24 /root/x11display/scripts/start_ch347_dirty_usb_x11.sh
```

The helpers require Bash (`set -euo pipefail` and `PIPESTATUS`). Execute them
directly through their Bash shebang, or use `bash script.sh`; do not invoke
them with `sh script.sh`.

Programs that should appear on the LCD must use the same display:

```bash
DISPLAY=:24 xterm &
DISPLAY=:24 xclock &
DISPLAY=:24 bash -lc 'your-app-here' &
```

The script captures `:24.0+0,0` with ffmpeg, converts to RGB565BE, and streams
the dirty output to the LCD. It does not replace the host's SSH or primary
desktop.

## Title Bars

Title bars are not drawn by Xvfb. They come from a window manager.

The MSYS-safe default does not start a second window manager:

```bash
APP=none WM=none /root/x11display/scripts/start_ch347_dirty_usb_x11.sh
```

`APP=none` is an explicit disable value: the daemon does not start
`glxgears` or any other demo application. Openbox remains available for a
standalone desktop smoke test, but must be requested explicitly:

```bash
APP=glxgears WM=openbox /root/x11display/scripts/start_ch347_dirty_usb_x11.sh
```

Use a different lightweight window manager if installed:

```bash
WM='matchbox-window-manager -use_titlebar yes'
```

## Touch Wiring

LCD:

```text
VCC          3.3V/VIO
GND          GND
CS           CH347F pin 13 / SCS0
SCK          CH347F pin 14 / SCK
SDI(MOSI)    MOSI
SDO(MISO)    MISO
DC           GPIO0
RESET        GPIO1
LED          GPIO2
```

XPT2046 resistive touch:

```text
T_CLK        SCK
T_CS         SCS1
T_SDI        MOSI
T_SDO        MISO
T_IRQ        GPIO3
```

The LCD and touch share SCK/MOSI/MISO. The LCD uses SCS0, touch uses SCS1.
Only one chip select may be active at a time.

## Touch Runtime

Touch is disabled by default. Enable it only after the XPT2046 wiring is in:

```bash
CH347_TOUCH=1 /root/x11display/scripts/start_ch347_dirty_usb_x11.sh
```

The dirty sink can poll GPIO3 as active-low IRQ, but on the current wiring GPIO3
stays high, so runtime defaults to raw XPT2046 polling:

```bash
CH347_TOUCH_USE_IRQ=0
```

When polling, it periodically deselects LCD CS0, selects touch CS1, slows SPI
for XPT2046, reads X/Y/Z over SPI, restores the LCD SPI clock and CS0, and
injects mouse events into the same X11 display through XTest.

The production default does not draw an LCD-side cursor. Touch injection must
not manufacture framebuffer damage when the application itself is static:

```bash
CH347_CURSOR=0
```

Explicitly enable the cursor only while debugging touch coordinates:

```bash
CH347_CURSOR=1
```

The default capture backend is event-driven (`CAPTURE=xdamage`,
`XCAP_OUTPUT=frame`). It always publishes one complete initial frame, even if
the existing static root window produces no XDamage event. `XCAP_IDLE_FPS=1`
then provides a bounded 1 FPS refresh heartbeat with or without touch, so a
static desktop cannot remain blank. Set it to `0` only when deliberately
disabling idle refresh. `XCAP_OUTPUT=rects` exists as an experimental
small-change path, but it is slower on glxgears because XDamage reports large
bounding boxes.

Change both limits live with
`bash scripts/set_fps.sh 60 --idle 1`; the capture process reloads
`XCAP_MAX_FPS` and `XCAP_IDLE_FPS` without restarting X11.

## Calibration

Run the calibration helper:

```bash
/root/x11display/scripts/calibrate_touch.sh
```

It stops any existing LCD stream, starts a fresh calibration session, and draws
5 cross targets. Tap and hold each target briefly. After the position points,
press the center lightly, then firmly, repeating that light/hard pair 3 times
for pressure calibration. The screen shows live raw X/Y, Z1/Z2, mapped X/Y, and
pressure while you press; the result is written to:

```text
/root/x11display/ch347/touch_calibration.env
```

Load it before a normal run:

```bash
set -a
. /root/x11display/ch347/touch_calibration.env
set +a
/root/x11display/scripts/start_ch347_dirty_usb_x11.sh
```

The calibration file contains:

```bash
CH347_TOUCH=1
CH347_TOUCH_SWAP_XY=0
CH347_TOUCH_INVERT_X=0
CH347_TOUCH_INVERT_Y=0
CH347_TOUCH_X_MIN=...
CH347_TOUCH_X_MAX=...
CH347_TOUCH_Y_MIN=...
CH347_TOUCH_Y_MAX=...
CH347_TOUCH_Z_MIN=...
CH347_TOUCH_PRESSURE_MIN=...
```

Useful calibration knobs:

```bash
CH347_CURSOR=1
CH347_TOUCH_CAL_FILE=/root/x11display/ch347/touch_calibration.env
CH347_TOUCH_CAL_MARGIN=32
CH347_TOUCH_SWAP_XY=0
CH347_TOUCH_INVERT_X=0
CH347_TOUCH_INVERT_Y=0
CH347_TOUCH_X_MIN=200
CH347_TOUCH_X_MAX=3900
CH347_TOUCH_Y_MIN=200
CH347_TOUCH_Y_MAX=3900
CH347_TOUCH_POLL_MS=25
CH347_TOUCH_CLOCK=5
CH347_TOUCH_RELEASE_SAMPLES=3
CH347_TOUCH_CAL_SAMPLES=4
CH347_TOUCH_DEBUG=1
CH347_USB_DEBUG=1
CH347_TOUCH_Z_MIN=20
CH347_TOUCH_PRESSURE_MIN=20
CH347_TOUCH_DISABLE_ON_ERRORS=0
```

Check whether GPIO3 IRQ is usable:

```bash
/root/x11display/scripts/stop_ch347_dirty_usb_x11.sh
/root/x11display/scripts/test_touch_irq.sh 20 15
```

Tap and release during the test. A usable active-low IRQ should show GPIO3
transitioning between high and low. If it reports `irq_never_low`, keep IRQ
disabled and use raw polling:

```bash
CH347_TOUCH=1 CH347_TOUCH_USE_IRQ=0 \
  /root/x11display/scripts/start_ch347_dirty_usb_x11.sh
```

If the cursor moves reversed, flip `CH347_TOUCH_INVERT_X` or
`CH347_TOUCH_INVERT_Y`. If X and Y are swapped, set `CH347_TOUCH_SWAP_XY=1`.

## Current Limitation

Touch sharing is cooperative with LCD streaming, not parallel. Polling runs
between displayed frames so it should not resurrect the old async deadlock, but
it will cost a little FPS when touching because CH347 has to switch CS and do
SPI reads.

## Touch and mouse modes

Normal startup now defaults to `CH347_TOUCH_MODE=touch`. In this mode a press
moves to the contact point, sends button down, tracks while held, and sends
button up on release. The LCD-side cursor is hidden after release, so a finger
tap behaves like direct touch instead of leaving a mouse pointer behind.

Switch modes while the display is running; no restart is needed:

```bash
/root/x11display/scripts/set_touch_mode.sh touch
/root/x11display/scripts/set_touch_mode.sh mouse
/root/x11display/scripts/set_touch_mode.sh toggle
/root/x11display/scripts/set_touch_mode.sh status
```

`mouse` preserves the old persistent cursor behavior. `touch` creates a real Linux HID touchscreen through `/dev/uhid`; Xorg and
libinput expose it as an XInput touch device, so browsers receive native touch
begin/update/end events. `mouse` uses the legacy XTest pointer path. Switching
modes does not restart X11. The mode control file defaults to
`/tmp/ch347_dirty_usb_x11/touch_mode` and is polled between touch samples.

The production X server is Xorg with the dummy video driver, not Xvfb. This is
required because Xorg hotplugs the UHID touchscreen through udev/libinput while
still providing the same in-memory 320x480 X11 screen captured by this project.
Required Debian packages are `xserver-xorg-core`,
`xserver-xorg-video-dummy`, `xserver-xorg-input-libinput`, and `xinput`.
