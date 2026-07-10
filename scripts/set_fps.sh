#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FPS_FILE="${CH347_FPS_FILE:-$PROJECT_DIR/ch347/fps.env}"
RUN_DIR="${RUN_DIR:-/tmp/ch347_dirty_usb_x11}"
PID_FILE="$RUN_DIR/pids"

usage()
{
    echo "usage: $0 FPS [--restart]" >&2
    echo "example: $0 60 --restart" >&2
    exit 2
}

[ $# -ge 1 ] && [ $# -le 2 ] || usage
fps="$1"
restart=0
[ "${2:-}" = "--restart" ] && restart=1
[ $# -eq 1 ] || [ "$restart" = "1" ] || usage

case "$fps" in
    ""|*[!0-9]*) usage ;;
esac
if [ "$fps" -lt 1 ] || [ "$fps" -gt 240 ]; then
    echo "FPS must be between 1 and 240" >&2
    exit 2
fi

tmp="$FPS_FILE.tmp.$$"
printf "FPS=%s\nXCAP_MAX_FPS=%s\n" "$fps" "$fps" > "$tmp"
mv -f "$tmp" "$FPS_FILE"

echo "capture FPS cap set to $fps"
echo "saved in: $FPS_FILE"

if [ -f "$PID_FILE" ]; then
    capture_pid=""
    while read -r pid; do
        if [ -r "/proc/$pid/exe" ] &&
                [ "$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)" = "$PROJECT_DIR/bin/xdamage_shm_capture" ]; then
            capture_pid="$pid"
            break
        fi
    done < "$PID_FILE"
    if [ -n "$capture_pid" ]; then
        kill -USR1 "$capture_pid"
        echo "live capture limit reloaded by PID $capture_pid; X11 was not restarted"
        restart=0
    fi
fi

if [ "$restart" = "1" ]; then
    touch_enabled=0
    debug_enabled=0
    if [ -f "$PID_FILE" ]; then
        daemon_pid="$(head -1 "$PID_FILE" 2>/dev/null || true)"
        if [ -n "$daemon_pid" ] && [ -r "/proc/$daemon_pid/environ" ]; then
            touch_enabled="$(tr "\0" "\n" < "/proc/$daemon_pid/environ" | sed -n "s/^CH347_TOUCH=//p" | tail -1)"
            debug_enabled="$(tr "\0" "\n" < "/proc/$daemon_pid/environ" | sed -n "s/^DEBUG=//p" | tail -1)"
        fi
    fi
    "$SCRIPT_DIR/stop_ch347_dirty_usb_x11.sh"
    sleep 1
    CH347_TOUCH="${touch_enabled:-0}" DEBUG="${debug_enabled:-0}" "$SCRIPT_DIR/start_ch347_dirty_usb_x11.sh"
fi

echo "This is an upper limit; full-screen FPS may remain SPI-bandwidth limited."
