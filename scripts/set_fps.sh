#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FPS_FILE="${CH347_FPS_FILE:-$PROJECT_DIR/ch347/fps.env}"
RUN_DIR="${RUN_DIR:-/tmp/ch347_dirty_usb_x11}"
PID_FILE="$RUN_DIR/pids"

usage()
{
    echo "usage: $0 FPS [--idle FPS] [--restart]" >&2
    echo "example: $0 60 --idle 0 --restart" >&2
    exit 2
}

[ $# -ge 1 ] || usage
fps="$1"
shift
restart=0
idle_fps=""
while [ $# -gt 0 ]; do
    case "$1" in
        --restart)
            restart=1
            shift
            ;;
        --idle)
            [ $# -ge 2 ] || usage
            idle_fps="$2"
            shift 2
            ;;
        *)
            usage
            ;;
    esac
done

case "$fps" in
    ""|*[!0-9]*) usage ;;
esac
if [ "$fps" -lt 1 ] || [ "$fps" -gt 240 ]; then
    echo "FPS must be between 1 and 240" >&2
    exit 2
fi
if [ -z "$idle_fps" ] && [ -f "$FPS_FILE" ]; then
    idle_fps="$(sed -n 's/^XCAP_IDLE_FPS=//p' "$FPS_FILE" | tail -1)"
fi
idle_fps="${idle_fps:-0}"
case "$idle_fps" in
    ""|*[!0-9]*) usage ;;
esac
if [ "$idle_fps" -lt 0 ] || [ "$idle_fps" -gt 60 ]; then
    echo "idle FPS must be between 0 and 60" >&2
    exit 2
fi

tmp="$FPS_FILE.tmp.$$"
printf "FPS=%s\nXCAP_MAX_FPS=%s\nXCAP_IDLE_FPS=%s\n" "$fps" "$fps" "$idle_fps" > "$tmp"
mv -f "$tmp" "$FPS_FILE"

echo "capture FPS cap set to $fps"
echo "capture idle FPS set to $idle_fps"
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
    bash "$SCRIPT_DIR/stop_ch347_dirty_usb_x11.sh"
    sleep 1
    CH347_TOUCH="${touch_enabled:-0}" DEBUG="${debug_enabled:-0}" \
        bash "$SCRIPT_DIR/start_ch347_dirty_usb_x11.sh"
fi

echo "This is an upper limit; full-screen FPS may remain SPI-bandwidth limited."
