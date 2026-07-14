#!/bin/sh
set -eu

for command in Xorg xdpyinfo xrandr; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "test_xorg_modes: missing $command" >&2
        exit 77
    fi
done

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=$(mktemp -d)
DISPLAY_NUMBER=${MSYS_XORG_MODE_TEST_DISPLAY:-99}
DISPLAY=:$DISPLAY_NUMBER
xorg_pid=

cleanup()
{
    if [ -n "$xorg_pid" ]; then
        kill "$xorg_pid" 2>/dev/null || true
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

Xorg "$DISPLAY" -noreset -nolisten tcp -novtswitch -sharevts \
    -config "$ROOT/xorg/xorg.conf" -logfile "$TMP/Xorg.log" \
    >"$TMP/stdout.log" 2>&1 &
xorg_pid=$!
i=0
until DISPLAY="$DISPLAY" xdpyinfo >/dev/null 2>&1; do
    i=$((i + 1))
    if [ "$i" -ge 50 ]; then
        cat "$TMP/stdout.log" "$TMP/Xorg.log" >&2
        exit 1
    fi
    sleep 0.05
done

DISPLAY="$DISPLAY" xrandr --size 480x320
dimensions=$(DISPLAY="$DISPLAY" xdpyinfo | awk '/dimensions:/{print $2; exit}')
if [ "$dimensions" != "480x320" ]; then
    echo "Xorg landscape mode mismatch: $dimensions" >&2
    exit 1
fi
DISPLAY="$DISPLAY" xrandr --size 320x480
dimensions=$(DISPLAY="$DISPLAY" xdpyinfo | awk '/dimensions:/{print $2; exit}')
if [ "$dimensions" != "320x480" ]; then
    echo "Xorg portrait mode mismatch: $dimensions" >&2
    exit 1
fi

echo "test_xorg_modes: ok"
