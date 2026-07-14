# 2026.7.10 stablev1 dirty-rendering audit

Reference archive: `../2026.7.10-stablev1.tar` (archive path relative to the
MSYS workspace, not this repository).

Archive SHA-256:
`66bf9d3ef23bfeec233439b2f745c7f3d4d7e91f36801e1cccee74e3f62f8575`.

## Result

The panel-side dirty implementation in `src/ch347_dirty_usb_sink.c` did not
regress after stablev1.  A source diff shows no changes to the tile comparison,
rectangle construction, dirty bounding-box calculation, full-area fallback, or
LCD rectangle submission functions.  The effective policy did regress because
the configured default for `CH347_MAX_RECTS` changed from `1` to `4` in three
places:

- `ch347/ch347_best_params.env`
- `scripts/start_ch347_dirty_usb_x11.sh`
- `scripts/ch347_dirty_usb_x11_daemon.sh`

Stablev1 deliberately uses one merged dirty bounding box.  On this CH347 link,
each extra ST7796 address window adds USB, command, and GPIO transitions.  The
recorded hardware measurements in `docs/NOTES.md` are about 9-10 output FPS for
the single-bbox path and about 3 FPS for the multi-window experiment, which also
looked stalled or uneven on the panel.

The three defaults above are therefore restored to `1`.  Explicit
`CH347_MAX_RECTS` environment overrides remain supported for experiments.
`CH347_FULL_AREA_PCT=40` and `CH347_STALE_MS=0` are unchanged.

## Intentionally retained post-stable fixes

The archive was not copied wholesale.  The following later behavior remains:

- frame rotation and inverse touch-coordinate mapping;
- one-shot processing of the frame prefetched while the sink waits for its
  mailbox producer; it follows the ordinary `new_frame` path, initializes the
  display-order base frame, and sends the normal first full frame immediately;
- display-order byte-swap handling for cursor-only mailbox passes;
- `XCAP_IDLE_FPS=0` event-driven idle behavior. `SIGUSR2` remains an explicit
  capture request, but is not the startup mechanism and does not force a
  physical panel write when the captured pixels already match the sink state;
- full-frame capture de-duplication before publishing to the mailbox;
- ignoring root `PropertyNotify` as pixel damage when XDamage is available;
- provider ownership, restart, rotation, and process-management changes.

Restoring the old `PropertyNotify` fallback or removing mailbox de-duplication
would reintroduce unrelated full-screen refreshes, so those capture-side changes
are not part of the dirty-policy rollback.

## Regression check

`tests/test_stable_dirty_defaults.sh` keeps the configuration file, both launch
layers, and the sink's built-in fallback aligned with the stable single-bbox
policy while confirming the stable full-area and stale-refresh defaults.
`tests/test_mailbox_prefetch.c` reproduces the consumed-sequence condition from
startup and verifies that the prefetched frame still produces one `new_frame`
edge before an unchanged mailbox becomes idle.
