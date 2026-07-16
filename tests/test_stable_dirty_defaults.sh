#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

fail()
{
    echo "test_stable_dirty_defaults: $*" >&2
    exit 1
}

expect_fixed_default()
{
    file=$1
    pattern=$2

    grep -F "$pattern" "$ROOT/$file" >/dev/null ||
        fail "$file does not preserve: $pattern"
}

# 2026.7.10-stablev1 uses one merged dirty bounding box.  Multiple LCD
# address windows are valid, but their CH347 command/GPIO overhead made the
# panel look stalled and reduced the measured output rate substantially.
expect_fixed_default ch347/ch347_best_params.env 'CH347_MAX_RECTS=1'
expect_fixed_default scripts/start_ch347_dirty_usb_x11.sh \
    'CH347_MAX_RECTS="${CH347_MAX_RECTS:-1}"'
expect_fixed_default scripts/ch347_dirty_usb_x11_daemon.sh \
    'CH347_MAX_RECTS="${CH347_MAX_RECTS:-1}"'
expect_fixed_default src/ch347_dirty_usb_sink.c \
    'env_u32("CH347_MAX_RECTS", 1)'

# Debug logging and the optional framebuffer overlay are independent. The
# production default must never manufacture overlay damage.
expect_fixed_default src/ch347_dirty_usb_sink.c \
    'env_u32("CH347_DEBUG", 0)'
expect_fixed_default src/ch347_dirty_usb_sink.c \
    'env_u32_range("CH347_DEBUG_OVERLAY", 0, 0, 1)'
expect_fixed_default ch347/ch347_best_params.env 'CH347_DEBUG_OVERLAY=0'
expect_fixed_default scripts/start_ch347_dirty_usb_x11.sh \
    'CH347_DEBUG_OVERLAY="${CH347_DEBUG_OVERLAY:-0}"'
expect_fixed_default scripts/ch347_dirty_usb_x11_daemon.sh \
    'CH347_DEBUG_OVERLAY="${CH347_DEBUG_OVERLAY:-0}"'

# Debug remains opt-in, while the opt-in default view includes aggregate CPU.
# Extending the overlay must not alter the stable dirty selector above.
expect_fixed_default ch347/debug_overlay.env 'CH347_DEBUG_OVERLAY_ITEMS=39'
expect_fixed_default scripts/start_ch347_dirty_usb_x11.sh \
    'CH347_DEBUG_OVERLAY_ITEMS="${CH347_DEBUG_OVERLAY_ITEMS:-fps,dirty,bytes,cpu}"'
expect_fixed_default scripts/ch347_dirty_usb_x11_daemon.sh \
    'CH347_DEBUG_OVERLAY_ITEMS="${CH347_DEBUG_OVERLAY_ITEMS:-fps,dirty,bytes,cpu}"'
expect_fixed_default scripts/ch347_display_config.sh \
    'ch347_config_uint "$value" 1 63 || return 1'

# Touch input is independent from panel damage.  A visible LCD-side cursor is
# useful for calibration/debugging only and must remain an explicit opt-in.
expect_fixed_default src/ch347_dirty_usb_sink.c \
    'env_u32("CH347_CURSOR", 0)'
expect_fixed_default ch347/ch347_best_params.env 'CH347_CURSOR=0'
expect_fixed_default scripts/start_ch347_dirty_usb_x11.sh \
    'CH347_CURSOR="${CH347_CURSOR:-0}"'
expect_fixed_default scripts/ch347_dirty_usb_x11_daemon.sh \
    'CH347_CURSOR="${CH347_CURSOR:-0}"'

# Keep the two other stable bbox thresholds aligned across configuration and
# launcher defaults.  Callers may still opt in to experiments through env.
expect_fixed_default ch347/ch347_best_params.env 'CH347_FULL_AREA_PCT=40'
expect_fixed_default ch347/ch347_best_params.env 'CH347_STALE_MS=0'
expect_fixed_default scripts/start_ch347_dirty_usb_x11.sh \
    'CH347_FULL_AREA_PCT="${CH347_FULL_AREA_PCT:-40}"'
expect_fixed_default scripts/start_ch347_dirty_usb_x11.sh \
    'CH347_STALE_MS="${CH347_STALE_MS:-0}"'
expect_fixed_default scripts/ch347_dirty_usb_x11_daemon.sh \
    'CH347_FULL_AREA_PCT="${CH347_FULL_AREA_PCT:-40}"'
expect_fixed_default scripts/ch347_dirty_usb_x11_daemon.sh \
    'CH347_STALE_MS="${CH347_STALE_MS:-0}"'

# A transport disconnect must not exhaust a small retry budget and tear down
# the still-healthy :24 X11 session.
expect_fixed_default ch347/ch347_best_params.env 'CH347_RESTART_MAX=0'
expect_fixed_default scripts/start_ch347_dirty_usb_x11.sh \
    'CH347_RESTART_MAX="${CH347_RESTART_MAX:-0}"'
expect_fixed_default scripts/ch347_dirty_usb_x11_daemon.sh \
    'CH347_RESTART_MAX="${CH347_RESTART_MAX:-0}"'

# A long 480M recovery failure is exported as one state edge for the optional
# MSYS provider. The standalone tree remains independent from any IPC stack.
expect_fixed_default scripts/ch347_dirty_usb_x11_daemon.sh \
    'CH347_LINK_STATE_FILE="${CH347_LINK_STATE_FILE:-$RUN_DIR/ch347-link-state.env}"'
expect_fixed_default scripts/ch347_dirty_usb_x11_daemon.sh \
    'publish_ch347_link_state degraded'
expect_fixed_default scripts/ch347_dirty_usb_x11_daemon.sh \
    'publish_ch347_link_state healthy'

# Match stablev1 capture pacing: while SPI is busy, retain at most one ready
# frame instead of continually overwriting mailbox slots with drag positions.
expect_fixed_default src/xdamage_shm_capture.c \
    'if (published > consumed + 1) {'
expect_fixed_default src/xdamage_shm_capture.c \
    'Keep one frame ready while USB is busy, but never build a stale queue.'
expect_fixed_default src/ch347_dirty_usb_sink.c \
    'full_pct=inactive(single-bbox)'
expect_fixed_default scripts/ch347_dirty_usb_x11_daemon.sh \
    'CH347_FULL_AREA_POLICY="inactive-single-bbox"'

echo "test_stable_dirty_defaults: ok"
