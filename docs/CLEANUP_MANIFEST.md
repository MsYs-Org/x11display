# Cleanup Manifest

This repository contains the Linux X11-to-CH347 SPI display and XPT2046 touch
transport. Application-specific media playback code stays outside the project.

## Kept In Project

Core source:

- `src/ch347_dirty_usb_sink.c`
- `src/ch347_st7796_test.c`
- `src/ch347_irq_test.c`
- `src/ch347_app_gate.c`
- `src/xdamage_shm_capture.c`

Runtime scripts:

- `scripts/start_ch347_dirty_usb_x11.sh`
- `scripts/stop_ch347_dirty_usb_x11.sh`
- `scripts/ch347_dirty_usb_x11_daemon.sh`
- `scripts/calibrate_touch.sh`
- `scripts/test_touch_irq.sh`
- `scripts/set_fps.sh`
- `scripts/check_env.sh`

CH347 support:

- `ch347/CH347LIB.h`
- `ch347/libch347spi.so`
- `ch347/ch347_best_params.env`
- `driver/ch347_fast_kernel/ch34x_pis.c`
- `driver/ch347_fast_kernel/Makefile`

## Removed Experiments

Raw full-screen video sinks, baked RGB565 assets, temporary Venus compatibility
libraries, duplicate launchers, and obsolete tuning scripts were removed after
a verified pre-cleanup archive was created.
