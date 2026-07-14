#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CAL_FILE="${CH347_TOUCH_CAL_FILE:-$PROJECT_DIR/ch347/touch_calibration.env}"
CAL_BACKUP="$CAL_FILE.before-filter"

bash "$SCRIPT_DIR/stop_ch347_dirty_usb_x11.sh" >/dev/null 2>&1 || true
[ -f "$CAL_FILE" ] && cp -f "$CAL_FILE" "$CAL_BACKUP"
sleep 1

export CH347_TOUCH=1
export CH347_TOUCH_CALIBRATE=1
export CH347_TOUCH_CAL_FILE="$CAL_FILE"
export CH347_TOUCH_CAL_EXIT="${CH347_TOUCH_CAL_EXIT:-1}"
export CH347_TOUCH_DEBUG="${CH347_TOUCH_DEBUG:-1}"
export CH347_USB_DEBUG="${CH347_USB_DEBUG:-1}"
export CH347_TOUCH_USE_IRQ="${CH347_TOUCH_USE_IRQ:-0}"
export CH347_TOUCH_CLOCK="${CH347_TOUCH_CLOCK:-5}"
export CH347_TOUCH_Z_MIN="${CH347_TOUCH_Z_MIN:-20}"
export CH347_TOUCH_PRESSURE_MIN="${CH347_TOUCH_PRESSURE_MIN:-20}"
export CH347_TOUCH_Z_STRICT="${CH347_TOUCH_Z_STRICT:-1}"
export CH347_TOUCH_CAL_SAMPLES="${CH347_TOUCH_CAL_SAMPLES:-8}"
export APP="${APP:-none}"
export WM="${WM:-none}"
export GATED="${GATED:-0}"
export FPS="${FPS:-10}"
export DEBUG="${DEBUG:-0}"
export CH347_RESTART_ON_FAIL="${CH347_RESTART_ON_FAIL:-0}"

bash "$SCRIPT_DIR/start_ch347_dirty_usb_x11.sh"

echo "touch calibration started"
echo "tap and briefly hold the 5 targets on the LCD"
echo "then press center: light, hard; repeat 3 times for pressure"
echo "calibration file: $CAL_FILE"
echo "log: /tmp/ch347_dirty_usb_x11/live.log"
