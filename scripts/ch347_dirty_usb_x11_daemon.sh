#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${PROJECT_DIR:-$(cd "$SCRIPT_DIR/.." && pwd)}"
BIN_DIR="${BIN_DIR:-$PROJECT_DIR/bin}"
CH347_DIR="${CH347_DIR:-$PROJECT_DIR/ch347}"

FPS="${FPS:-30}"
CAPTURE="${CAPTURE:-xdamage}"
XCAP_MAX_FPS="${XCAP_MAX_FPS:-$FPS}"
XCAP_DEBUG="${XCAP_DEBUG:-0}"
XCAP_OUTPUT="${XCAP_OUTPUT:-frame}"
XCAP_FPS_FILE="${XCAP_FPS_FILE:-$PROJECT_DIR/ch347/fps.env}"
XCAP_NICE="${XCAP_NICE:--5}"
CH347_SINK_NICE="${CH347_SINK_NICE:--5}"
XVFB_NICE="${XVFB_NICE:-5}"
XSERVER="${XSERVER:-Xorg}"
XORG_CONFIG="${XORG_CONFIG:-$PROJECT_DIR/xorg/xorg.conf}"
APP_NICE="${APP_NICE:-12}"
MAX_FRAMES="${MAX_FRAMES:-0}"
DEPTH="${DEPTH:-8}"
APP="${APP:-glxgears}"
WM="${WM:-openbox}"
DISPLAY_ID="${DISPLAY_ID:-:24}"
WIDTH="${WIDTH:-320}"
HEIGHT="${HEIGHT:-480}"
PIXFMT="${PIXFMT:-rgb565be}"
DEBUG="${DEBUG:-0}"
GATED="${GATED:-0}"
RENDER_MS="${RENDER_MS:-30}"
GLXGEARS_RENDER_MS="${GLXGEARS_RENDER_MS:-8}"
PACKET_US="${PACKET_US:-0}"
CH347_MODE="${CH347_MODE:-0}"
CH347_CLOCK="${CH347_CLOCK:-1}"
CH347_ARM="${CH347_ARM:-$BIN_DIR/ch347_st7796_test}"
CH347_SINK="${CH347_SINK:-$BIN_DIR/ch347_dirty_usb_sink}"
XCAP_BIN="${XCAP_BIN:-$BIN_DIR/xdamage_shm_capture}"
CH347_FULL_AREA_PCT="${CH347_FULL_AREA_PCT:-40}"
CH347_MAX_RECTS="${CH347_MAX_RECTS:-1}"
CH347_STALE_MS="${CH347_STALE_MS:-0}"
CH347_STALE_BUDGET="${CH347_STALE_BUDGET:-60}"
CH347_HOLD_CS="${CH347_HOLD_CS:-1}"
CH347_LATEST_ONLY="${CH347_LATEST_ONLY:-1}"
CH347_URB_TIMEOUT_MS="${CH347_URB_TIMEOUT_MS:-1500}"
CH347_USB_DEBUG="${CH347_USB_DEBUG:-0}"
CH347_GPIO_OVERLAY="${CH347_GPIO_OVERLAY:-0}"
CH347_GPIO_OVERLAY_MS="${CH347_GPIO_OVERLAY_MS:-200}"
CH347_RESTART_ON_FAIL="${CH347_RESTART_ON_FAIL:-1}"
CH347_RESTART_DELAY_SEC="${CH347_RESTART_DELAY_SEC:-2}"
CH347_RESTART_MAX="${CH347_RESTART_MAX:-3}"
CH347_TOUCH="${CH347_TOUCH:-0}"
CH347_TOUCH_USE_IRQ="${CH347_TOUCH_USE_IRQ:-0}"
CH347_TOUCH_MODE="${CH347_TOUCH_MODE:-touch}"
CH347_CURSOR="${CH347_CURSOR:-1}"
CH347_TOUCH_CALIBRATE="${CH347_TOUCH_CALIBRATE:-0}"
CH347_TOUCH_CAL_FILE="${CH347_TOUCH_CAL_FILE:-$PROJECT_DIR/ch347/touch_calibration.env}"
CH347_TOUCH_CAL_EXIT="${CH347_TOUCH_CAL_EXIT:-1}"
CH347_TOUCH_CAL_MARGIN="${CH347_TOUCH_CAL_MARGIN:-32}"
CH347_TOUCH_POLL_MS="${CH347_TOUCH_POLL_MS:-32}"
CH347_TOUCH_SWAP_XY="${CH347_TOUCH_SWAP_XY:-0}"
CH347_TOUCH_INVERT_X="${CH347_TOUCH_INVERT_X:-0}"
CH347_TOUCH_INVERT_Y="${CH347_TOUCH_INVERT_Y:-0}"
CH347_TOUCH_X_MIN="${CH347_TOUCH_X_MIN:-200}"
CH347_TOUCH_X_MAX="${CH347_TOUCH_X_MAX:-3900}"
CH347_TOUCH_Y_MIN="${CH347_TOUCH_Y_MIN:-200}"
CH347_TOUCH_Y_MAX="${CH347_TOUCH_Y_MAX:-3900}"
CH347_TOUCH_WIDTH="${CH347_TOUCH_WIDTH:-$WIDTH}"
CH347_TOUCH_HEIGHT="${CH347_TOUCH_HEIGHT:-$HEIGHT}"
CH347_TOUCH_MOVE_THRESH="${CH347_TOUCH_MOVE_THRESH:-2}"
CH347_TOUCH_JUMP_THRESH="${CH347_TOUCH_JUMP_THRESH:-160}"
CH347_TOUCH_FILTER_WEIGHT="${CH347_TOUCH_FILTER_WEIGHT:-1}"
CH347_TOUCH_MAX_ERRORS="${CH347_TOUCH_MAX_ERRORS:-8}"
CH347_TOUCH_CLOCK="${CH347_TOUCH_CLOCK:-5}"
CH347_TOUCH_RELEASE_SAMPLES="${CH347_TOUCH_RELEASE_SAMPLES:-3}"
CH347_TOUCH_CAL_SAMPLES="${CH347_TOUCH_CAL_SAMPLES:-4}"
CH347_TOUCH_Z_MIN="${CH347_TOUCH_Z_MIN:-50}"
CH347_TOUCH_PRESSURE_MIN="${CH347_TOUCH_PRESSURE_MIN:-$CH347_TOUCH_Z_MIN}"
CH347_TOUCH_Z_STRICT="${CH347_TOUCH_Z_STRICT:-1}"
CH347_TOUCH_DEBUG="${CH347_TOUCH_DEBUG:-0}"
CH347_TOUCH_DISABLE_ON_ERRORS="${CH347_TOUCH_DISABLE_ON_ERRORS:-0}"
if [ -z "${XCAP_IDLE_FPS:-}" ]; then
    XCAP_IDLE_FPS=0
fi
USB_SYS="${CH347_USB_SYS:-/sys/bus/usb/devices/1-1.1}"
IFACE_ID="${CH347_IFACE_ID:-1-1.1:1.4}"
CH347_WAIT_SEC="${CH347_WAIT_SEC:-12}"
RUN_DIR="${RUN_DIR:-/tmp/ch347_dirty_usb_x11}"
PID_FILE="$RUN_DIR/pids"
CH347_TOUCH_MODE_FILE="${CH347_TOUCH_MODE_FILE:-$RUN_DIR/touch_mode}"
FRAME_MAILBOX="${CH347_FRAME_MAILBOX:-$RUN_DIR/frame.mailbox}"
LOG_FILE="$RUN_DIR/live.log"
mkdir -p "$RUN_DIR"
if [ ! -f "$CH347_TOUCH_MODE_FILE" ]; then
    printf '%s\n' "$CH347_TOUCH_MODE" > "$CH347_TOUCH_MODE_FILE"
fi

XVFB_PID=""
WM_PID=""
APP_PID=""
GATE_PID=""
UNBOUND=0
STOP_REQUESTED=0

discover_ch347()
{
    local iface
    local base
    local devdir

    for iface in /sys/bus/usb/devices/*:1.4; do
        [ -e "$iface" ] || continue
        base="$(basename "$iface")"
        devdir="/sys/bus/usb/devices/${base%:*}"
        [ -r "$devdir/idVendor" ] || continue
        [ -r "$devdir/idProduct" ] || continue
        [ "$(cat "$devdir/idVendor")" = "1a86" ] || continue
        [ "$(cat "$devdir/idProduct")" = "55de" ] || continue

        USB_SYS="$devdir"
        IFACE_ID="$base"
        return 0
    done

    return 1
}

wait_ch347_bound()
{
    local tries=$((CH347_WAIT_SEC * 4))

    if [ "$tries" -lt 4 ]; then
        tries=4
    fi

    for _ in $(seq 1 "$tries"); do
        discover_ch347 || true

        if [ -n "$IFACE_ID" ] && [ -d /sys/bus/usb/drivers/ch34x_pis ] &&
                [ ! -e "/sys/bus/usb/drivers/ch34x_pis/$IFACE_ID" ]; then
            printf '%s' "$IFACE_ID" > /sys/bus/usb/drivers/ch34x_pis/bind 2>/dev/null || true
        fi

        if [ -d "$USB_SYS" ] && [ -e /dev/ch34x_pis0 ] &&
                lsusb -t | grep -q 'ch34x_pis, 480M'; then
            return 0
        fi

        sleep 0.25
    done

    lsusb -t >&2 || true
    echo "CH347 did not settle as ch34x_pis at 480M within ${CH347_WAIT_SEC}s." >&2
    return 1
}

stop_x_stack()
{
    set +e
    [ -n "$GATE_PID" ] && kill "$GATE_PID" 2>/dev/null || true
    [ -n "$APP_PID" ] && kill -CONT "$APP_PID" 2>/dev/null || true
    [ -n "$APP_PID" ] && kill "$APP_PID" 2>/dev/null || true
    [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null || true
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null || true
    GATE_PID=""
    APP_PID=""
    WM_PID=""
    XVFB_PID=""
}

cleanup()
{
    set +e
    stop_x_stack

    if [ "$UNBOUND" = "1" ] &&
            [ -d /sys/bus/usb/drivers/ch34x_pis ] &&
            [ ! -e "/sys/bus/usb/drivers/ch34x_pis/$IFACE_ID" ]; then
        printf '%s' "$IFACE_ID" > /sys/bus/usb/drivers/ch34x_pis/bind 2>/dev/null || true
    fi

    rm -f "$PID_FILE"
}

handle_term()
{
    STOP_REQUESTED=1
    cleanup
    exit 0
}

trap cleanup EXIT
trap handle_term INT TERM

start_x_stack()
{
    local xnum
    local period
    local render_s
    local rest_s
    local gate_render_ms

    xnum="${DISPLAY_ID#:}"
    xnum="${xnum%%.*}"
    if [ "$XSERVER" = "Xvfb" ]; then
        nice -n "$XVFB_NICE" Xvfb "$DISPLAY_ID" -screen 0 "${WIDTH}x${HEIGHT}x24" -nolisten tcp -dumbSched >>"$LOG_FILE" 2>&1 &
    else
        nice -n "$XVFB_NICE" Xorg "$DISPLAY_ID" -noreset -nolisten tcp -novtswitch -sharevts -config "$XORG_CONFIG" -logfile "$RUN_DIR/Xorg.log" >>"$LOG_FILE" 2>&1 &
    fi

    XVFB_PID="$!"
    echo "$XVFB_PID" >> "$PID_FILE"

    for _ in $(seq 1 40); do
        if DISPLAY="$DISPLAY_ID" xdpyinfo >/dev/null 2>&1; then
            break
        fi
        sleep 0.1
    done

    if ! DISPLAY="$DISPLAY_ID" xdpyinfo >/dev/null 2>&1; then
        echo "X server failed to become ready on $DISPLAY_ID" >>"$LOG_FILE"
        return 1
    fi

    case "$WM" in
        none)
            WM_PID=""
            ;;
        openbox)
            DISPLAY="$DISPLAY_ID" nice -n "$APP_NICE" openbox >>"$LOG_FILE" 2>&1 &
            WM_PID="$!"
            ;;
        *)
            DISPLAY="$DISPLAY_ID" nice -n "$APP_NICE" bash -lc "$WM" >>"$LOG_FILE" 2>&1 &
            WM_PID="$!"
            ;;
    esac
    if [ -n "$WM_PID" ]; then
        echo "$WM_PID" >> "$PID_FILE"
    fi

    case "$APP" in
        glxgears)
            DISPLAY="$DISPLAY_ID" LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=1 GALLIUM_THREAD=0 vblank_mode=0 \
                nice -n "$APP_NICE" \
                glxgears -geometry "${WIDTH}x${HEIGHT}+0+0" >>"$LOG_FILE" 2>&1 &
            APP_PID="$!"
            ;;
        none)
            APP_PID=""
            ;;
        *)
            DISPLAY="$DISPLAY_ID" nice -n "$APP_NICE" bash -lc "$APP" >>"$LOG_FILE" 2>&1 &
            APP_PID="$!"
            ;;
    esac

    if [ -n "$APP_PID" ]; then
        echo "$APP_PID" >> "$PID_FILE"
    fi
    sleep 2

    if [ "$GATED" = "1" ] && [ -n "$APP_PID" ]; then
        kill -STOP "$APP_PID" 2>/dev/null || true

        period="$(awk -v fps="$FPS" 'BEGIN { if (fps <= 0) fps = 1; printf "%.6f", 1.0 / fps }')"
        gate_render_ms="$RENDER_MS"
        if [ "$APP" = "glxgears" ]; then
            gate_render_ms="$GLXGEARS_RENDER_MS"
        fi
        render_s="$(awk -v ms="$gate_render_ms" 'BEGIN { if (ms < 1) ms = 1; printf "%.6f", ms / 1000.0 }')"
        rest_s="$(awk -v p="$period" -v r="$render_s" 'BEGIN { v = p - r; if (v < 0.001) v = 0.001; printf "%.6f", v }')"

        (
            while :; do
                kill -CONT "$APP_PID" 2>/dev/null || exit 0
                sleep "$render_s"
                kill -STOP "$APP_PID" 2>/dev/null || exit 0
                sleep "$rest_s"
            done
        ) &
        GATE_PID="$!"
        echo "$GATE_PID" >> "$PID_FILE"
    fi
}

ensure_x_stack()
{
    if [ -n "$XVFB_PID" ] && kill -0 "$XVFB_PID" 2>/dev/null &&
            DISPLAY="$DISPLAY_ID" xdpyinfo >/dev/null 2>&1; then
        return 0
    fi

    echo "dirty_usb_x11_restart_x display=$DISPLAY_ID" >>"$LOG_FILE"
    stop_x_stack
    start_x_stack
}

mkdir -p "$RUN_DIR"
echo "$$" > "$PID_FILE"

wait_ch347_bound

echo "dirty_usb_x11_start capture=$CAPTURE fps=$FPS xcap_max_fps=$XCAP_MAX_FPS xcap_idle_fps=$XCAP_IDLE_FPS xcap_output=$XCAP_OUTPUT transport=shm-mailbox max_frames=$MAX_FRAMES depth=$DEPTH app=$APP wm=$WM display=$DISPLAY_ID pixfmt=$PIXFMT gated=$GATED render_ms=$RENDER_MS packet_us=$PACKET_US clock=$CH347_CLOCK full_pct=$CH347_FULL_AREA_PCT max_rects=$CH347_MAX_RECTS stale_ms=$CH347_STALE_MS stale_budget=$CH347_STALE_BUDGET hold_cs=$CH347_HOLD_CS latest_only=$CH347_LATEST_ONLY touch=$CH347_TOUCH touch_irq=$CH347_TOUCH_USE_IRQ cursor=$CH347_CURSOR calibrate=$CH347_TOUCH_CALIBRATE gpio_overlay=$CH347_GPIO_OVERLAY gpio_overlay_ms=$CH347_GPIO_OVERLAY_MS urb_timeout_ms=$CH347_URB_TIMEOUT_MS restart_on_fail=$CH347_RESTART_ON_FAIL sink=$CH347_SINK" >>"$LOG_FILE"
start_x_stack

run_stream_once()
{
    local bus
    local dev
    local usb_dev
    local ffmpeg_rc
    local cap_rc
    local sink_rc
    local cap_pid
    local sink_pid

    ensure_x_stack || return 1
    wait_ch347_bound || return 1
    LD_LIBRARY_PATH="$CH347_DIR:${LD_LIBRARY_PATH:-}" \
        "$CH347_ARM" "$CH347_MODE" "$CH347_CLOCK" arm 480 0 raw >>"$LOG_FILE" 2>&1 || return 1
    wait_ch347_bound || return 1

    bus="$(cat "$USB_SYS/busnum")"
    dev="$(cat "$USB_SYS/devnum")"
    usb_dev="$(printf '/dev/bus/usb/%03d/%03d' "$bus" "$dev")"

    if [ -e "/sys/bus/usb/drivers/ch34x_pis/$IFACE_ID" ]; then
        printf '%s' "$IFACE_ID" > /sys/bus/usb/drivers/ch34x_pis/unbind
        UNBOUND=1
    fi

    set +e
    case "$CAPTURE" in
        xdamage)
            rm -f "$FRAME_MAILBOX"
            MAX_FRAMES="$MAX_FRAMES" WIDTH="$WIDTH" HEIGHT="$HEIGHT" \
            XCAP_MAX_FPS="$XCAP_MAX_FPS" XCAP_IDLE_FPS="$XCAP_IDLE_FPS" \
            XCAP_DEBUG="$XCAP_DEBUG" XCAP_OUTPUT=frame \
            XCAP_FPS_FILE="$XCAP_FPS_FILE" \
            XCAP_MAILBOX="$FRAME_MAILBOX" \
                nice -n "$XCAP_NICE" \
                "$XCAP_BIN" "$DISPLAY_ID" "$WIDTH" "$HEIGHT" \
                    "$XCAP_MAX_FPS" "$XCAP_IDLE_FPS" >>"$LOG_FILE" 2>&1 &
            cap_pid="$!"

            CH347_USB_DEV="$usb_dev" CH347_DEBUG="$DEBUG" CH347_PACKET_US="$PACKET_US" \
              CH347_FRAME_MAILBOX="$FRAME_MAILBOX" \
              CH347_FULL_AREA_PCT="$CH347_FULL_AREA_PCT" CH347_MAX_RECTS="$CH347_MAX_RECTS" \
              CH347_STALE_MS="$CH347_STALE_MS" CH347_STALE_BUDGET="$CH347_STALE_BUDGET" \
              CH347_HOLD_CS="$CH347_HOLD_CS" CH347_LATEST_ONLY=1 \
              CH347_URB_TIMEOUT_MS="$CH347_URB_TIMEOUT_MS" CH347_USB_DEBUG="$CH347_USB_DEBUG" \
              CH347_GPIO_OVERLAY="$CH347_GPIO_OVERLAY" CH347_GPIO_OVERLAY_MS="$CH347_GPIO_OVERLAY_MS" \
              CH347_TOUCH="$CH347_TOUCH" CH347_TOUCH_USE_IRQ="$CH347_TOUCH_USE_IRQ" \
              CH347_TOUCH_MODE="$CH347_TOUCH_MODE" CH347_TOUCH_MODE_FILE="$CH347_TOUCH_MODE_FILE" \
              CH347_CURSOR="$CH347_CURSOR" CH347_TOUCH_CALIBRATE="$CH347_TOUCH_CALIBRATE" \
              CH347_TOUCH_CAL_FILE="$CH347_TOUCH_CAL_FILE" CH347_TOUCH_CAL_EXIT="$CH347_TOUCH_CAL_EXIT" \
              CH347_TOUCH_CAL_MARGIN="$CH347_TOUCH_CAL_MARGIN" CH347_TOUCH_POLL_MS="$CH347_TOUCH_POLL_MS" \
              CH347_TOUCH_SWAP_XY="$CH347_TOUCH_SWAP_XY" CH347_TOUCH_INVERT_X="$CH347_TOUCH_INVERT_X" \
              CH347_TOUCH_INVERT_Y="$CH347_TOUCH_INVERT_Y" CH347_TOUCH_X_MIN="$CH347_TOUCH_X_MIN" \
              CH347_TOUCH_X_MAX="$CH347_TOUCH_X_MAX" CH347_TOUCH_Y_MIN="$CH347_TOUCH_Y_MIN" \
              CH347_TOUCH_Y_MAX="$CH347_TOUCH_Y_MAX" CH347_TOUCH_WIDTH="$CH347_TOUCH_WIDTH" \
              CH347_TOUCH_HEIGHT="$CH347_TOUCH_HEIGHT" CH347_TOUCH_MOVE_THRESH="$CH347_TOUCH_MOVE_THRESH" \
              CH347_TOUCH_JUMP_THRESH="$CH347_TOUCH_JUMP_THRESH" CH347_TOUCH_FILTER_WEIGHT="$CH347_TOUCH_FILTER_WEIGHT" \
              CH347_TOUCH_MAX_ERRORS="$CH347_TOUCH_MAX_ERRORS" CH347_TOUCH_CLOCK="$CH347_TOUCH_CLOCK" \
              CH347_TOUCH_RELEASE_SAMPLES="$CH347_TOUCH_RELEASE_SAMPLES" CH347_TOUCH_CAL_SAMPLES="$CH347_TOUCH_CAL_SAMPLES" \
              CH347_TOUCH_Z_MIN="$CH347_TOUCH_Z_MIN" CH347_TOUCH_PRESSURE_MIN="$CH347_TOUCH_PRESSURE_MIN" \
              CH347_TOUCH_Z_STRICT="$CH347_TOUCH_Z_STRICT" CH347_TOUCH_DEBUG="$CH347_TOUCH_DEBUG" \
              CH347_TOUCH_DISABLE_ON_ERRORS="$CH347_TOUCH_DISABLE_ON_ERRORS" DISPLAY_ID="$DISPLAY_ID" \
              nice -n "$CH347_SINK_NICE" "$CH347_SINK" "$DEPTH" "$MAX_FRAMES" &
            sink_pid="$!"
            echo "$cap_pid" >> "$PID_FILE"
            echo "$sink_pid" >> "$PID_FILE"
            wait "$sink_pid"
            sink_rc="$?"
            kill "$cap_pid" 2>/dev/null || true
            wait "$cap_pid" 2>/dev/null
            cap_rc="$?"
            ffmpeg_rc="$cap_rc"
            ;;
        ffmpeg)
            ffmpeg -hide_banner -loglevel warning \
                -f x11grab -draw_mouse 0 -video_size "${WIDTH}x${HEIGHT}" -framerate "$FPS" \
                -i "${DISPLAY_ID}.0+0,0" \
                -vf "format=${PIXFMT}" -pix_fmt "$PIXFMT" -f rawvideo - \
            | CH347_USB_DEV="$usb_dev" CH347_DEBUG="$DEBUG" CH347_PACKET_US="$PACKET_US" \
              CH347_FULL_AREA_PCT="$CH347_FULL_AREA_PCT" CH347_MAX_RECTS="$CH347_MAX_RECTS" \
              CH347_STALE_MS="$CH347_STALE_MS" CH347_STALE_BUDGET="$CH347_STALE_BUDGET" \
              CH347_HOLD_CS="$CH347_HOLD_CS" \
              CH347_LATEST_ONLY="$CH347_LATEST_ONLY" \
              CH347_URB_TIMEOUT_MS="$CH347_URB_TIMEOUT_MS" \
              CH347_USB_DEBUG="$CH347_USB_DEBUG" \
              CH347_GPIO_OVERLAY="$CH347_GPIO_OVERLAY" \
              CH347_GPIO_OVERLAY_MS="$CH347_GPIO_OVERLAY_MS" \
              CH347_TOUCH="$CH347_TOUCH" \
              CH347_TOUCH_USE_IRQ="$CH347_TOUCH_USE_IRQ" \
              CH347_TOUCH_MODE="$CH347_TOUCH_MODE" \
              CH347_TOUCH_MODE_FILE="$CH347_TOUCH_MODE_FILE" \
              CH347_CURSOR="$CH347_CURSOR" \
              CH347_TOUCH_CALIBRATE="$CH347_TOUCH_CALIBRATE" \
              CH347_TOUCH_CAL_FILE="$CH347_TOUCH_CAL_FILE" \
              CH347_TOUCH_CAL_EXIT="$CH347_TOUCH_CAL_EXIT" \
              CH347_TOUCH_CAL_MARGIN="$CH347_TOUCH_CAL_MARGIN" \
              CH347_TOUCH_POLL_MS="$CH347_TOUCH_POLL_MS" \
              CH347_TOUCH_SWAP_XY="$CH347_TOUCH_SWAP_XY" \
              CH347_TOUCH_INVERT_X="$CH347_TOUCH_INVERT_X" \
              CH347_TOUCH_INVERT_Y="$CH347_TOUCH_INVERT_Y" \
              CH347_TOUCH_X_MIN="$CH347_TOUCH_X_MIN" \
              CH347_TOUCH_X_MAX="$CH347_TOUCH_X_MAX" \
              CH347_TOUCH_Y_MIN="$CH347_TOUCH_Y_MIN" \
              CH347_TOUCH_Y_MAX="$CH347_TOUCH_Y_MAX" \
              CH347_TOUCH_WIDTH="$CH347_TOUCH_WIDTH" \
              CH347_TOUCH_HEIGHT="$CH347_TOUCH_HEIGHT" \
              CH347_TOUCH_MOVE_THRESH="$CH347_TOUCH_MOVE_THRESH" \
              CH347_TOUCH_JUMP_THRESH="$CH347_TOUCH_JUMP_THRESH" \
              CH347_TOUCH_FILTER_WEIGHT="$CH347_TOUCH_FILTER_WEIGHT" \
              CH347_TOUCH_MAX_ERRORS="$CH347_TOUCH_MAX_ERRORS" \
              CH347_TOUCH_CLOCK="$CH347_TOUCH_CLOCK" \
              CH347_TOUCH_RELEASE_SAMPLES="$CH347_TOUCH_RELEASE_SAMPLES" \
              CH347_TOUCH_CAL_SAMPLES="$CH347_TOUCH_CAL_SAMPLES" \
              CH347_TOUCH_Z_MIN="$CH347_TOUCH_Z_MIN" \
              CH347_TOUCH_PRESSURE_MIN="$CH347_TOUCH_PRESSURE_MIN" \
              CH347_TOUCH_Z_STRICT="$CH347_TOUCH_Z_STRICT" \
              CH347_TOUCH_DEBUG="$CH347_TOUCH_DEBUG" \
              CH347_TOUCH_DISABLE_ON_ERRORS="$CH347_TOUCH_DISABLE_ON_ERRORS" \
              DISPLAY_ID="$DISPLAY_ID" \
              nice -n "$CH347_SINK_NICE" \
              "$CH347_SINK" "$DEPTH" "$MAX_FRAMES"
            ;;
        *)
            echo "unknown CAPTURE=$CAPTURE" >>"$LOG_FILE"
            false
            ;;
    esac
    if [ "$CAPTURE" = ffmpeg ]; then
        PIPE_RC=("${PIPESTATUS[@]}")
        cap_rc="${PIPE_RC[0]}"
        sink_rc="${PIPE_RC[1]}"
        ffmpeg_rc="$cap_rc"
    fi
    set -e
    echo "dirty_usb_x11_stream_exit capture=$CAPTURE cap=$cap_rc ffmpeg=$ffmpeg_rc sink=$sink_rc" >>"$LOG_FILE"

    if [ -d /sys/bus/usb/drivers/ch34x_pis ] &&
            [ ! -e "/sys/bus/usb/drivers/ch34x_pis/$IFACE_ID" ]; then
        printf '%s' "$IFACE_ID" > /sys/bus/usb/drivers/ch34x_pis/bind 2>/dev/null || true
        UNBOUND=0
    fi

    return "$sink_rc"
}

restart_sleep()
{
    local left="$CH347_RESTART_DELAY_SEC"

    while [ "$left" -gt 0 ]; do
        [ -f "$PID_FILE" ] || {
            STOP_REQUESTED=1
            return 1
        }
        sleep 1
        left=$((left - 1))
    done

    return 0
}

RESTARTS=0
while :; do
    set +e
    run_stream_once
    rc="$?"
    set -e

    if [ "$rc" = "0" ]; then
        echo "dirty_usb_x11_exit restarts=$RESTARTS status=0" >>"$LOG_FILE"
        exit 0
    fi

    echo "dirty_usb_x11_restart rc=$rc count=$RESTARTS" >>"$LOG_FILE"

    if [ "$STOP_REQUESTED" = "1" ] || [ ! -f "$PID_FILE" ]; then
        echo "dirty_usb_x11_exit restarts=$RESTARTS status=stopped" >>"$LOG_FILE"
        exit 0
    fi

    if [ "$CH347_RESTART_ON_FAIL" != "1" ] || [ "$MAX_FRAMES" != "0" ]; then
        echo "dirty_usb_x11_exit restarts=$RESTARTS status=$rc" >>"$LOG_FILE"
        exit "$rc"
    fi

    if [ "$RESTARTS" -ge "$CH347_RESTART_MAX" ]; then
        echo "dirty_usb_x11_exit restarts=$RESTARTS status=max_restarts" >>"$LOG_FILE"
        exit "$rc"
    fi

    RESTARTS=$((RESTARTS + 1))
    restart_sleep || {
        echo "dirty_usb_x11_exit restarts=$RESTARTS status=stopped" >>"$LOG_FILE"
        exit 0
    }
done
