# CH347 驱动编译安装说明

本文记录本机 `/root/USB-HS-Bridge` 目录下 CH347 Linux 驱动的编译、修改、安装和验证过程。

## 1. 当前环境

设备环境：

```bash
uname -a
```

实际内核：

```text
Linux openstick 5.15.0-handsomekernel+ #7 SMP PREEMPT Sat Apr 30 13:33:59 CST 2022 aarch64 GNU/Linux
```

内核头文件路径已存在：

```bash
ls -ld /lib/modules/$(uname -r)/build
```

实际结果：

```text
/lib/modules/5.15.0-handsomekernel+/build -> /usr/src/linux-headers-5.15.0-handsomekernel+
```

这一步很关键，内核模块必须使用当前正在运行的内核对应的 headers 编译，否则模块可能无法加载。

## 2. 驱动源码位置

USB-HS-Bridge 仓库中真正用于 Linux 内核模块编译的源码位于：

```text
/root/USB-HS-Bridge/doc/CH347Par_Linux_V1.02_HID/CH347Par_Linux_V1.02_HID/Driver
```

核心文件：

```text
Makefile
ch34x_pis.c
```

生成的模块名：

```text
ch34x_pis.ko
```

## 3. 为什么原始驱动需要补一处匹配

当前插入的 USB 设备识别结果是：

```bash
lsusb
```

对应设备：

```text
Bus 001 Device 010: ID 1a86:55de QinHeng Electronics UART+SPI+I2C+JTAG
```

进一步查看 USB 描述符后可以看到，这个设备是 `1a86:55de`，并且 SPI/I2C/JTAG/GPIO 对应的是 vendor-specific 的 interface 4：

```text
idVendor  0x1a86
idProduct 0x55de
bNumInterfaces 5
InterfaceNumber 4
bInterfaceClass 255 Vendor Specific Class
```

原始 `ch34x_pis.c` 里已经支持：

```c
{ USB_DEVICE_INTERFACE_NUMBER(0x1A86, 0x55DB, 0x02) },
{ USB_DEVICE_INTERFACE_NUMBER(0x1A86, 0x55DD, 0x02) },
```

但没有匹配当前设备的 `0x55DE`。因此即使模块能编译，也不会自动绑定当前 CH347F 设备。

为了解决这个问题，在：

```text
/root/USB-HS-Bridge/doc/CH347Par_Linux_V1.02_HID/CH347Par_Linux_V1.02_HID/Driver/ch34x_pis.c
```

中增加了 CH347F 的接口号定义：

```c
#define CH347F_INTERFACE_NUM	0x04
```

并在 `ch34x_usb_ids[]` 中加入：

```c
{ USB_DEVICE_INTERFACE_NUMBER(0x1A86, 0x55DE, CH347F_INTERFACE_NUM) },		// CH347F UART+SPI+IIC+JTAG
```

这样生成的内核模块就会包含以下 alias：

```text
alias: usb:v1A86p55DEd*dc*dsc*dp*ic*isc*ip*in04*
```

这就是这次能成功绑定 `/dev/ch34x_pis0` 的关键。

## 4. 编译步骤

进入驱动目录：

```bash
cd /root/USB-HS-Bridge/doc/CH347Par_Linux_V1.02_HID/CH347Par_Linux_V1.02_HID/Driver
```

清理旧产物并重新编译：

```bash
make clean
make
```

编译过程中出现了若干 warning，例如格式化输出类型不匹配、忽略 `copy_to_user/copy_from_user` 返回值等。这些 warning 来自厂商源码本身，本次没有影响模块生成。

成功生成：

```text
ch34x_pis.ko
```

## 5. 安装步骤

在驱动目录执行：

```bash
make install
```

实际安装位置：

```text
/lib/modules/5.15.0-handsomekernel+/kernel/drivers/usb/misc/ch34x_pis.ko
```

`make install` 同时执行了：

```bash
depmod -a
```

这会更新 `/lib/modules/$(uname -r)/modules.alias` 等模块索引文件，让系统可以通过 USB ID 自动匹配到 `ch34x_pis`。

## 6. 加载模块

安装后手动加载：

```bash
modprobe ch34x_pis
```

检查模块是否已加载：

```bash
lsmod | grep ch34x_pis
```

实际结果：

```text
ch34x_pis              24576  0
```

## 7. 开机自动加载

为了重启后自动加载模块，创建了：

```text
/etc/modules-load.d/ch34x_pis.conf
```

内容：

```text
ch34x_pis
```

下次启动时 systemd/modules-load 会自动加载该模块。

## 8. 验证安装结果

查看模块信息：

```bash
modinfo ch34x_pis
```

关键结果：

```text
filename: /lib/modules/5.15.0-handsomekernel+/kernel/drivers/usb/misc/ch34x_pis.ko
alias:    usb:v1A86p55DEd*dc*dsc*dp*ic*isc*ip*in04*
alias:    usb:v1A86p55DDd*dc*dsc*dp*ic*isc*ip*in02*
alias:    usb:v1A86p55DBd*dc*dsc*dp*ic*isc*ip*in02*
alias:    usb:v1A86p5512d*dc*dsc*dp*ic*isc*ip*in*
name:     ch34x_pis
vermagic: 5.15.0-handsomekernel+ SMP preempt mod_unload aarch64
```

查看设备节点：

```bash
ls -l /dev/ch34x_pis*
```

实际结果：

```text
crw------- 1 root root 180, 200 /dev/ch34x_pis0
```

查看 USB interface 绑定情况：

```bash
cat /sys/bus/usb/devices/1-1.1:1.4/uevent
```

关键结果：

```text
DRIVER=ch34x_pis
PRODUCT=1a86/55de/120
INTERFACE=255/0/0
MODALIAS=usb:v1A86p55DEd0120dcEFdsc02dp01icFFisc00ip00in04
```

这说明当前 CH347F 的 vendor-specific interface 4 已经被 `ch34x_pis` 驱动绑定成功。

## 9. 成功标志

满足以下几项即可认为安装成功：

1. `modinfo ch34x_pis` 能看到 `1A86:55DE` 的 alias。
2. `lsmod` 能看到 `ch34x_pis`。
3. `/sys/bus/usb/devices/.../uevent` 中显示 `DRIVER=ch34x_pis`。
4. `/dev/` 下生成 `/dev/ch34x_pis0`。

本机当前已经满足以上条件。

## 10. 常用维护命令

卸载当前模块：

```bash
modprobe -r ch34x_pis
```

重新加载：

```bash
modprobe ch34x_pis
```

查看内核日志：

```bash
dmesg | tail -80
```

重新编译并安装：

```bash
cd /root/USB-HS-Bridge/doc/CH347Par_Linux_V1.02_HID/CH347Par_Linux_V1.02_HID/Driver
make clean
make
make install
modprobe -r ch34x_pis
modprobe ch34x_pis
```

查看模块 alias 是否包含 55DE：

```bash
modinfo ch34x_pis | grep 55DE
```

## 11. 注意事项

1. 如果以后升级或更换内核，需要用新内核对应的 headers 重新编译并安装。
2. 当前设备节点权限是 `crw------- root root`，普通用户默认不能直接访问。如果需要普通用户访问，可以后续添加 udev 规则。
3. 本次驱动只绑定 `1a86:55de` 的 interface 4，不会接管前面的 UART CDC interface。
4. 编译时提示的 gcc 版本和内核构建 gcc 版本不同，但模块已正常加载，当前不影响使用。

