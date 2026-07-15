#!/bin/sh
set -eu

for command in Xvfb xdpyinfo od; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "test_latest_mailbox: missing $command" >&2
        exit 77
    fi
done

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CAPTURE_BIN=${XCAP_TEST_BIN:-$ROOT/bin/xdamage_shm_capture}
TMP=$(mktemp -d)
DISPLAY_NUMBER=${MSYS_LATEST_MAILBOX_TEST_DISPLAY:-97}
DISPLAY=:$DISPLAY_NUMBER
xvfb_pid=
capture_pid=

cleanup()
{
    if [ -n "$capture_pid" ]; then
        kill "$capture_pid" 2>/dev/null || true
        wait "$capture_pid" 2>/dev/null || true
    fi
    if [ -n "$xvfb_pid" ]; then
        kill "$xvfb_pid" 2>/dev/null || true
        wait "$xvfb_pid" 2>/dev/null || true
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

Xvfb "$DISPLAY" -screen 0 320x480x24 -ac -nolisten tcp \
    >"$TMP/xvfb.log" 2>&1 &
xvfb_pid=$!
i=0
until DISPLAY="$DISPLAY" xdpyinfo >/dev/null 2>&1; do
    i=$((i + 1))
    if [ "$i" -ge 50 ]; then
        cat "$TMP/xvfb.log" >&2
        exit 1
    fi
    sleep 0.05
done

DISPLAY="$DISPLAY" XCAP_OUTPUT=frame XCAP_MAILBOX="$TMP/frame.mailbox" \
MAX_FRAMES=0 XCAP_MAX_FPS=120 XCAP_IDLE_FPS=0 \
    "$CAPTURE_BIN" "$DISPLAY" 320 480 120 0 \
    >"$TMP/capture.log" 2>&1 &
capture_pid=$!

i=0
while [ ! -s "$TMP/frame.mailbox" ]; do
    i=$((i + 1))
    if [ "$i" -ge 50 ]; then
        cat "$TMP/capture.log" >&2
        exit 1
    fi
    sleep 0.02
done

# SIGUSR2 asks for a real publication even when the pixels are unchanged.
# Leave consumed_seq at zero to model a panel occupied by one large SPI rect.
# The old one-frame FIFO policy stopped at publication 2; latest-only
# coalescing must continue replacing mailbox slots.
for _ in 1 2 3 4 5 6 7 8; do
    kill -USR2 "$capture_pid"
    sleep 0.03
done
sleep 0.05

published=$(od -An -tu8 -j24 -N8 "$TMP/frame.mailbox" | tr -d ' ')
consumed=$(od -An -tu8 -j32 -N8 "$TMP/frame.mailbox" | tr -d ' ')
if [ "${published:-0}" -lt 5 ]; then
    echo "latest mailbox stalled behind consumer: published=${published:-0}" >&2
    cat "$TMP/capture.log" >&2
    exit 1
fi
if [ "${consumed:-1}" -ne 0 ]; then
    echo "test changed consumer edge unexpectedly: consumed=$consumed" >&2
    exit 1
fi

echo "test_latest_mailbox: ok published=$published consumed=$consumed"
