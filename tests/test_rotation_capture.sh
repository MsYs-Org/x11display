#!/bin/sh
set -eu

for command in Xvfb xdpyinfo xsetroot od; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "test_rotation_capture: missing $command" >&2
        exit 77
    fi
done

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CAPTURE_BIN=${XCAP_TEST_BIN:-$ROOT/bin/xdamage_shm_capture}
TMP=$(mktemp -d)
DISPLAY_NUMBER=${MSYS_ROTATION_TEST_DISPLAY:-98}
DISPLAY=:$DISPLAY_NUMBER
xvfb_pid=
capture_pid=

cleanup()
{
    if [ -n "$xvfb_pid" ]; then
        kill "$xvfb_pid" 2>/dev/null || true
    fi
    if [ -n "$capture_pid" ]; then
        kill "$capture_pid" 2>/dev/null || true
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

Xvfb "$DISPLAY" -screen 0 480x320x24 -ac -nolisten tcp \
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

DISPLAY="$DISPLAY" xsetroot -solid '#336699'
DISPLAY="$DISPLAY" MAX_FRAMES=1 XCAP_OUTPUT=frame XCAP_ROTATION=right \
XCAP_MAILBOX="$TMP/frame.mailbox" \
    "$CAPTURE_BIN" "$DISPLAY" 480 320 60 0

# frame_mailbox_header begins with magic, version, width, height, frame_bytes.
set -- $(od -An -tu4 -j8 -N12 "$TMP/frame.mailbox")
if [ "$1" -ne 320 ] || [ "$2" -ne 480 ] || [ "$3" -ne 307200 ]; then
    echo "rotated mailbox mismatch width=$1 height=$2 bytes=$3" >&2
    exit 1
fi

# Reusing one capture process across a same-size right/left switch exercises
# the live SHM-image replacement path. XShmCreateImage retains the descriptor
# address, so replacing it with a short-lived stack object used to produce a
# BadShmSeg and silently force the daemon into stream recovery.
printf 'CH347_DISPLAY_ROTATION=right\n' >"$TMP/rotation.env"
DISPLAY="$DISPLAY" XCAP_OUTPUT=frame XCAP_ROTATION=right \
XCAP_ROTATION_FILE="$TMP/rotation.env" XCAP_MAILBOX="$TMP/live.mailbox" \
    "$CAPTURE_BIN" "$DISPLAY" 480 320 60 0 \
    >"$TMP/live.out" 2>"$TMP/live.err" &
capture_pid=$!
i=0
until [ -f "$TMP/live.mailbox" ]; do
    i=$((i + 1))
    if [ "$i" -ge 50 ]; then
        cat "$TMP/live.err" >&2
        exit 1
    fi
    sleep 0.05
done
printf 'CH347_DISPLAY_ROTATION=left\n' >"$TMP/rotation.env"
kill -USR1 "$capture_pid"
DISPLAY="$DISPLAY" xsetroot -solid '#663399'
i=0
until grep -q 'rotation=left .* (live)' "$TMP/live.err"; do
    kill -0 "$capture_pid" 2>/dev/null || {
        cat "$TMP/live.err" >&2
        exit 1
    }
    i=$((i + 1))
    if [ "$i" -ge 50 ]; then
        cat "$TMP/live.err" >&2
        exit 1
    fi
    sleep 0.05
done
sleep 0.1
kill -0 "$capture_pid"
kill "$capture_pid"
wait "$capture_pid" 2>/dev/null || true
capture_pid=

echo "test_rotation_capture: ok"
