#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RUN_DIR="${RUN_DIR:-/tmp/ch347_dirty_usb_x11}"
PID_FILE="$RUN_DIR/pids"

if [ -f "$PID_FILE" ]; then
    while read -r pid; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            echo "display stream is running; stop it first:" >&2
            echo "  $SCRIPT_DIR/stop_ch347_dirty_usb_x11.sh" >&2
            exit 1
        fi
    done < "$PID_FILE"
fi

exec "$PROJECT_DIR/bin/ch347_irq_test" \
    "${CH347_IRQ_DEV:-/dev/ch34x_pis0}" \
    "${1:-20}" \
    "${2:-15}"
