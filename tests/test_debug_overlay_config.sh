#!/bin/bash
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMPDIR_TEST=$(mktemp -d)
trap 'rm -rf "$TMPDIR_TEST"' EXIT HUP INT TERM

# shellcheck disable=SC1090
. "$ROOT/scripts/ch347_display_config.sh"

config="$TMPDIR_TEST/debug-overlay.env"
ch347_write_debug_overlay_config "$config" 1 176 1 63 1000
ch347_read_debug_overlay_config "$config"
[ "$CH347_CONFIG_OVERLAY_ITEMS" = 63 ]
[ "$(ch347_debug_overlay_items_text 63)" = \
    "fps,dirty,bytes,bbox,memory,cpu" ]

if ch347_write_debug_overlay_config "$config" 1 176 1 64 1000; then
    echo "test_debug_overlay_config: mask 64 was accepted" >&2
    exit 1
fi

echo "test_debug_overlay_config: ok"
