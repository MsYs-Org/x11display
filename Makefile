CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2

ROOT := $(CURDIR)
SRC := $(ROOT)/src
BIN := $(ROOT)/bin
CH347 := $(ROOT)/ch347

.PHONY: all test integration-test clean driver driver-clean install-driver

all: \
	$(BIN)/ch347_dirty_usb_sink \
	$(BIN)/ch347_st7796_test \
	$(BIN)/ch347_irq_test \
	$(BIN)/ch347_app_gate \
	$(BIN)/xdamage_shm_capture

$(BIN):
	mkdir -p $@

$(BIN)/ch347_dirty_usb_sink: $(SRC)/ch347_dirty_usb_sink.c $(SRC)/frame_mailbox.h $(SRC)/frame_rotation.h $(SRC)/touch_affine.h | $(BIN)
	$(CC) $(CFLAGS) -pthread -o $@ $< -ldl -lm

$(BIN)/ch347_st7796_test: $(SRC)/ch347_st7796_test.c $(CH347)/CH347LIB.h $(CH347)/libch347spi.so | $(BIN)
	$(CC) $(CFLAGS) -I$(CH347) -Wl,-rpath,'$$ORIGIN/../ch347' -o $@ $(SRC)/ch347_st7796_test.c -L$(CH347) -lch347spi

$(BIN)/ch347_irq_test: $(SRC)/ch347_irq_test.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/ch347_app_gate: $(SRC)/ch347_app_gate.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/xdamage_shm_capture: $(SRC)/xdamage_shm_capture.c $(SRC)/frame_mailbox.h $(SRC)/frame_rotation.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $< -lX11 -lXext -lXdamage

$(BIN)/test_frame_rotation: tests/test_frame_rotation.c $(SRC)/frame_rotation.h | $(BIN)
	$(CC) $(CFLAGS) -I$(SRC) -o $@ $<

$(BIN)/test_touch_affine: tests/test_touch_affine.c $(SRC)/touch_affine.h | $(BIN)
	$(CC) $(CFLAGS) -I$(SRC) -o $@ $< -lm

$(BIN)/test_mailbox_prefetch: tests/test_mailbox_prefetch.c \
		$(SRC)/ch347_dirty_usb_sink.c $(SRC)/frame_mailbox.h \
		$(SRC)/frame_rotation.h | $(BIN)
	$(CC) $(CFLAGS) -I$(SRC) -pthread -o $@ $< -ldl

test: $(BIN)/test_frame_rotation $(BIN)/test_touch_affine $(BIN)/test_mailbox_prefetch
	$(BIN)/test_frame_rotation
	$(BIN)/test_touch_affine
	$(BIN)/test_mailbox_prefetch
	bash tests/test_debug_overlay_config.sh
	sh tests/test_stable_dirty_defaults.sh

integration-test: $(BIN)/xdamage_shm_capture
	sh tests/test_mailbox_backpressure.sh
	sh tests/test_rotation_capture.sh
	sh tests/test_xorg_modes.sh

driver:
	$(MAKE) -C $(ROOT)/driver/ch347_fast_kernel

install-driver:
	$(MAKE) -C $(ROOT)/driver/ch347_fast_kernel install

driver-clean:
	$(MAKE) -C $(ROOT)/driver/ch347_fast_kernel clean

clean:
	rm -f $(BIN)/ch347_dirty_usb_sink \
		$(BIN)/ch347_st7796_test \
		$(BIN)/ch347_irq_test \
		$(BIN)/ch347_app_gate \
		$(BIN)/xdamage_shm_capture \
		$(BIN)/test_frame_rotation \
		$(BIN)/test_touch_affine \
		$(BIN)/test_mailbox_prefetch
