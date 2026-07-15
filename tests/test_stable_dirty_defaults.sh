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

echo "test_stable_dirty_defaults: ok"
