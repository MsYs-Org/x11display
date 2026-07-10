#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

need_cmd()
{
    if command -v "$1" >/dev/null 2>&1; then
        printf 'ok   %s\n' "$1"
    else
        printf 'miss %s\n' "$1"
    fi
}

printf 'project: %s\n' "$PROJECT_DIR"
printf 'kernel:  %s\n' "$(uname -r)"
printf 'arch:    %s\n' "$(uname -m)"

for cmd in gcc make Xvfb xdpyinfo openbox lsusb; do
    need_cmd "$cmd"
done

if ldconfig -p 2>/dev/null | grep -q 'libX11.so.6'; then
    echo 'ok   libX11 runtime'
else
    echo 'miss libX11 runtime'
fi

if ldconfig -p 2>/dev/null | grep -q 'libXtst.so.6'; then
    echo 'ok   libXtst runtime for touch injection'
else
    echo 'warn libXtst runtime missing; CH347_TOUCH=1 will be disabled'
fi

if [ -d /sys/bus/usb/drivers/ch34x_pis ]; then
    echo 'ok   ch34x_pis driver directory'
else
    echo 'miss ch34x_pis driver directory'
fi

if [ -e /dev/ch34x_pis0 ]; then
    echo 'ok   /dev/ch34x_pis0'
else
    echo 'miss /dev/ch34x_pis0'
fi

if lsusb -t 2>/dev/null | grep -q 'ch34x_pis, 480M'; then
    echo 'ok   CH347 bound at 480M'
else
    echo 'warn CH347 not currently shown as ch34x_pis at 480M'
    lsusb -t 2>/dev/null || true
fi

echo
echo 'binaries:'
for f in \
    "$PROJECT_DIR/bin/ch347_st7796_test" \
    "$PROJECT_DIR/bin/ch347_dirty_usb_sink" \
    "$PROJECT_DIR/bin/ch347_irq_test" \
    "$PROJECT_DIR/bin/xdamage_shm_capture"; do
    if [ -x "$f" ]; then
        printf 'ok   %s\n' "$f"
    else
        printf 'miss %s\n' "$f"
    fi
done
