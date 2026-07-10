#!/bin/bash
set -euo pipefail

RUN_DIR="${RUN_DIR:-/tmp/ch347_dirty_usb_x11}"
MODE_FILE="${CH347_TOUCH_MODE_FILE:-$RUN_DIR/touch_mode}"
mode="${1:-status}"

case "$mode" in
    touch|mouse)
        mkdir -p "$(dirname "$MODE_FILE")"
        tmp="$MODE_FILE.$$"
        printf "%s\n" "$mode" > "$tmp"
        mv -f "$tmp" "$MODE_FILE"
        echo "touch input mode: $mode"
        ;;
    status)
        if [ -r "$MODE_FILE" ]; then
            read -r current < "$MODE_FILE" || current=touch
        else
            current=touch
        fi
        echo "touch input mode: $current"
        ;;
    toggle)
        if [ -r "$MODE_FILE" ]; then
            read -r current < "$MODE_FILE" || current=touch
        else
            current=touch
        fi
        if [ "$current" = touch ]; then
            exec "$0" mouse
        else
            exec "$0" touch
        fi
        ;;
    *)
        echo "usage: $0 {touch|mouse|toggle|status}" >&2
        exit 2
        ;;
esac
