# CH347 Driver Notes

The project includes a local patched CH347 kernel driver under:

```text
driver/ch347_fast_kernel/
```

The important files are:

- `ch34x_pis.c`
- `Makefile`
- optionally a prebuilt `ch34x_pis.ko` for this exact kernel

The prebuilt module in this tree was built for:

```text
5.15.0-handsomekernel+
```

On a different machine or kernel, rebuild it.

## Build

```bash
cd /root/x11display
make driver
```

This expects:

```text
/lib/modules/$(uname -r)/build
```

to exist. Install matching kernel headers if it does not.

## Install

```bash
cd /root/x11display
make install-driver
modprobe ch34x_pis || insmod driver/ch347_fast_kernel/ch34x_pis.ko
```

Verify:

```bash
ls -l /dev/ch34x_pis0
lsusb -t
```

Expected:

```text
If 4, Class=Vendor Specific Class, Driver=ch34x_pis, 480M
```

## Binding

The runtime scripts discover CH347 by USB vendor/product:

```text
idVendor  = 1a86
idProduct = 55de
interface = :1.4
```

They bind the kernel driver for initialization, then temporarily unbind it when
userspace claims the raw USB interface with `USBDEVFS_CLAIMINTERFACE`.

## Safety

The scripts should not touch SSH, networking, package state, or system services.
They only:

- start/stop their own Xvfb/app/ffmpeg/sink processes
- bind/unbind the CH347 interface 4
- write logs under `/tmp/ch347_dirty_usb_x11` or `x11display/logs`

If a run goes wrong:

```bash
/root/x11display/scripts/stop_ch347_dirty_usb_x11.sh
```

Then rebind manually if needed:

```bash
printf '1-1.1:1.4' > /sys/bus/usb/drivers/ch34x_pis/bind
```

Use the actual interface id from `ls /sys/bus/usb/devices/*:1.4` on another
machine.
