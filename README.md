# x11display

这是一个运行在 Linux 上的 X11 显示与触摸桥接项目，用来把一个独立的 320x480 桌面输出到通过 CH347F 连接的 SPI LCD，并把电阻触摸屏输入再映射回同一个 X11 桌面。

它的核心目标不是“直接驱动一块屏”，而是搭一条完整链路：

- 在本机启动一个独立的小尺寸 X11 桌面
- 捕获桌面变化区域
- 转换为 LCD 需要的 RGB565BE 像素流
- 通过 CH347 的 USB Bulk SPI 通道推到 LCD
- 可选地读取 XPT2046 触摸数据并注入回 X11

当前仓库偏向“能稳定跑起来并持续迭代”的工程形态，包含用户态程序、启动脚本、驱动、Xorg dummy 配置和触摸校准脚本。

## 项目结构

- `src/`
  - `xdamage_shm_capture.c`：从 X11 捕获画面，使用 XDamage + MIT-SHM，把最新帧写入共享 mailbox
  - `ch347_dirty_usb_sink.c`：读取最新帧，做脏区分析，并通过 CH347 输出到 LCD
  - `ch347_st7796_test.c`：LCD 初始化、模式设置、基础输出测试
  - `ch347_irq_test.c`：测试触摸 IRQ/GPIO
  - `ch347_app_gate.c`：早期用于应用节流/门控
- `scripts/`
  - `start_ch347_dirty_usb_x11.sh`：主入口，启动整套链路
  - `ch347_dirty_usb_x11_daemon.sh`：真正管理 X server、捕获、sink、USB 绑定和自动重启
  - `stop_ch347_dirty_usb_x11.sh`：停止运行中的链路
  - `calibrate_touch.sh`：触摸校准
  - `set_touch_mode.sh`：在 `touch` / `mouse` 模式间切换
  - `check_env.sh`：检查依赖和 CH347 环境
- `driver/ch347_fast_kernel/`
  - 本地补丁版 CH347 内核驱动 `ch34x_pis`
- `xorg/xorg.conf`
  - 320x480 的 Xorg dummy 显示配置
- `docs/`
  - 记录驱动、触摸、开发结论和调试说明
- `ch347/`
  - 厂商库、默认参数、FPS 配置、触摸校准文件

## 它是怎么工作的

当前默认链路如下：

```text
应用程序
  -> Xorg dummy (:24, 320x480)
  -> 默认不启动窗口管理器（WM=none）
  -> XDamage + MIT-SHM 捕获
  -> 共享内存 mailbox 中转最新帧
  -> ch347_dirty_usb_sink 脏区分析
  -> CH347 USB Bulk SPI
  -> ST7796 LCD
```

如果接了 XPT2046 触摸屏，则还有反向输入链路：

```text
XPT2046
  -> CH347 SPI 读取
  -> 坐标/压力映射
  -> 注入到 :24 这个 X11 桌面
```

## 当前默认行为

这部分很重要，因为仓库里保留了不少历史路径。

- 默认 X server 是 `Xorg`，不是 `Xvfb`
- 默认捕获方式是 `CAPTURE=xdamage`
- 默认输出协议是 `XCAP_OUTPUT=frame`
- 默认窗口管理器是 `WM=none`；MSYS 自己的窗口策略不会被 Openbox 接管
- 默认空闲刷新是 `XCAP_IDLE_FPS=0`，首帧后只响应真实 XDamage 或显式恢复请求
- 默认显示分辨率是 `320x480`
- 默认触摸模式文件会写到 `/tmp/ch347_dirty_usb_x11/touch_mode`
- 默认触摸是关闭的，需要连线后显式启用

也就是说，今天这套系统的主路径是：

- `Xorg dummy` 提供一个可热插拔输入设备的 X11 环境
- `xdamage_shm_capture` 在 SPI 忙时最多保留一个待发送帧；达到 stablev1
  的 mailbox 上限后等待 sink 完成当前矩形，不额外覆盖拖动中间态
- `ch347_dirty_usb_sink` 计算脏区并推送到 LCD

## 为什么要做“脏区输出”

SPI LCD 的带宽有限，而 CH347 上每次 GPIO / 命令切换也有额外 USB 开销。

所以项目没有简单地每帧全屏重刷，而是采用：

- 维护当前帧和上一帧
- 找出发生变化的区域
- 小变化时只刷脏区
- 变化太大时回退到全屏或大矩形输出

当前稳定策略不是“很多小矩形”，而是“一个合并后的脏框 + 大面积时全屏回退”。原因是：

- ST7796 虽然支持多个窗口
- 但 CH347 这条链路上，每多一个窗口就多一组命令和 GPIO 切换
- 实测多矩形模式比单脏框更慢

默认推荐：

```bash
CH347_MAX_RECTS=1
CH347_FULL_AREA_PCT=40
```

The sink emits a bounded `dirty_stats` record on the first processed frame and
every 30 processed frames. It reports exact transmitted pixels, sent versus
zero-damage passes, and cumulative full/large refresh counts. The counters do
not change damage selection and remain available when the on-panel debug
overlay is disabled, so observing refresh behaviour does not itself dirty the
display.

## 硬件连接

当前用户态控制路径使用的引脚约定：

- LCD `DC` -> CH347 `GPIO0`
- LCD `RESET` -> CH347 `GPIO1`
- LCD `LED/背光` -> CH347 `GPIO2`
- LCD `CS` -> CH347 `SCS0`
- Touch `T_CS` -> CH347 `SCS1`
- Touch `T_IRQ` -> CH347 `GPIO3`
- SPI 数据包按 CH347 `0xC4` 事务发送

LCD 与 XPT2046 共享：

- `SCK`
- `MOSI`
- `MISO`

它们通过不同的 CS 进行切换，因此同一时刻只能选中一个设备。

## 依赖

基础构建和运行依赖：

```bash
apt-get update
apt-get install -y \
  build-essential \
  usbutils \
  x11-utils \
  xserver-xorg-core \
  xserver-xorg-video-dummy \
  xserver-xorg-input-libinput \
  xinput \
  libx11-6 \
  libxtst6 \
  libx11-dev \
  libxext-dev \
  libxdamage-dev \
  libxfixes-dev
```

说明：

- `xserver-xorg-core` 和 `xserver-xorg-video-dummy` 是当前默认路径需要的
- `openbox` 不是默认依赖；只有显式设置 `WM=openbox` 时才需要
- `glxgears` 也不是 MSYS 依赖；只有显式设置 `APP=glxgears` 做冒烟测试时才需要
- `libxtst6` 用于旧的鼠标注入路径
- 若启用 `touch` 模式下的原生触摸，还依赖系统的 `udev/libinput` 热插拔链路

## 编译

在项目根目录执行：

```bash
make
```

会生成：

- `bin/ch347_dirty_usb_sink`
- `bin/ch347_st7796_test`
- `bin/ch347_irq_test`
- `bin/ch347_app_gate`
- `bin/xdamage_shm_capture`

其中：

- `ch347_st7796_test` 链接 `ch347/libch347spi.so`
- rpath 已按相对路径写好，便于整目录迁移

## 驱动构建与安装

项目自带本地补丁版 CH347 内核驱动：

- `driver/ch347_fast_kernel/ch34x_pis.c`

构建：

```bash
make driver
```

安装：

```bash
make install-driver
modprobe ch34x_pis || insmod driver/ch347_fast_kernel/ch34x_pis.ko
```

检查：

```bash
ls -l /dev/ch34x_pis0
lsusb -t
```

理想状态：

- 存在 `/dev/ch34x_pis0`
- `lsusb -t` 中能看到 CH347 的 `interface 4` 绑定到 `ch34x_pis`
- 速率显示为 `480M`

如果机器内核和仓库内预编译模块不匹配，需要重新编译驱动。

## 快速开始

所有 `scripts/*.sh` 都是 Bash 脚本，并使用 `set -euo pipefail`。请直接执行
脚本或显式使用 `bash scripts/xxx.sh`，不要用 `sh scripts/xxx.sh`。启动脚本
也会显式通过 Bash 拉起后台 daemon，确保 `PIPESTATUS` 等 Bash 语义有效。

### 1. 检查环境

```bash
./scripts/check_env.sh
```

### 2. 编译

```bash
make
```

### 3. 启动

```bash
./scripts/start_ch347_dirty_usb_x11.sh
```

默认会：

- 启动 `:24` 这个 X11 显示
- 不拉起第三方窗口管理器（`WM=none`）
- 默认启动一个 `glxgears` 作为测试应用
- 开始捕获并把画面推到 LCD

MSYS 启动显示 provider 时使用 `APP=none WM=none`。`APP=none` 是明确的
禁用值，不会启动 `glxgears`；需要独立冒烟测试时才显式使用
`APP=glxgears`。

### 4. 停止

```bash
./scripts/stop_ch347_dirty_usb_x11.sh
```

## 在 LCD 对应的 X11 桌面里运行程序

只要把程序的 `DISPLAY` 指向 `:24` 即可：

```bash
DISPLAY=:24 xterm &
DISPLAY=:24 xclock &
DISPLAY=:24 your-app &
```

说明：

- `:24` 是一个独立桌面，不会替换宿主机当前 SSH 或主图形桌面
- 默认没有 Openbox 标题栏，窗口布局由上层 MSYS 窗口策略负责
- 如需独立调试传统桌面，可显式启用 Openbox

例如：

```bash
APP=none WM=none ./scripts/start_ch347_dirty_usb_x11.sh
APP=glxgears WM=openbox ./scripts/start_ch347_dirty_usb_x11.sh
```

## 常用运行参数

推荐的稳定默认值大致如下：

```bash
CH347_CLOCK=1
CAPTURE=xdamage
XCAP_MAX_FPS=60
XCAP_OUTPUT=frame
PIXFMT=rgb565be
FPS=60
GATED=0
CH347_MAX_RECTS=1
CH347_FULL_AREA_PCT=40
CH347_HOLD_CS=1
CH347_LATEST_ONLY=1
CH347_STALE_MS=0
APP=none
WM=none
XCAP_IDLE_FPS=1
CH347_TOUCH=0
CH347_TOUCH_CLOCK=5
```

关键参数含义：

- `CH347_CLOCK=1`：当前更稳定，通常对应 30 MHz
- `CAPTURE=xdamage`：事件驱动捕获，启动时无条件输出一张完整首帧
- `XCAP_IDLE_FPS=1`：没有触摸和 XDamage 时仍以 1 FPS 刷新，避免静态桌面不亮
- `XCAP_MAX_FPS`：捕获上限
- `FPS`：sink 目标帧率上限
- `CH347_MAX_RECTS=1`：只发送一个合并脏框
- `CH347_LATEST_ONLY=1`：只消费最新帧，降低延迟
- `GATED=0`：默认不再启用 SIGSTOP/SIGCONT 应用门控

运行中可以同时调整活动帧率上限和空闲刷新率，不重启 X11：

```bash
bash ./scripts/set_fps.sh 60 --idle 1
```

配置会原子写入 `ch347/fps.env`，并通过 `SIGUSR1` 让捕获进程同时重载
`XCAP_MAX_FPS` 与 `XCAP_IDLE_FPS`。

## 触摸支持

默认关闭。接好 XPT2046 后可启用：

```bash
CH347_TOUCH=1 ./scripts/start_ch347_dirty_usb_x11.sh
```

当前触摸支持两种模式：

- `touch`：原生触摸行为
- `mouse`：兼容旧的鼠标注入行为

切换方式：

```bash
./scripts/set_touch_mode.sh touch
./scripts/set_touch_mode.sh mouse
./scripts/set_touch_mode.sh toggle
./scripts/set_touch_mode.sh status
```

补充说明：

- `touch` 是默认模式
- `mouse` 模式使用 XTest 指针注入
- 触摸轮询与显示输出是协作式共享 SPI，不是并行双通道

如果 GPIO3 的 IRQ 不可靠，可禁用 IRQ，改为轮询：

```bash
CH347_TOUCH=1 CH347_TOUCH_USE_IRQ=0 ./scripts/start_ch347_dirty_usb_x11.sh
```

## 触摸校准

执行：

```bash
./scripts/calibrate_touch.sh
```

它会：

- 先停止当前运行中的显示链路
- 进入 5 点校准
- 额外采集轻压/重压样本
- 把结果写入 `ch347/touch_calibration.env`

正常运行时，如果检测到这个文件，启动脚本会自动加载它。

## 调试与辅助功能

### GPIO 状态叠加显示

```bash
CH347_GPIO_OVERLAY=1 CH347_GPIO_OVERLAY_MS=100 \
  ./scripts/start_ch347_dirty_usb_x11.sh
```

说明：

- 会在 LCD 上显示 GPIO0..GPIO7 状态
- 轮询过快会增加 USB 开销，通常 `100-200ms` 比较合适

### 切回旧的 ffmpeg 抓屏路径

```bash
CAPTURE=ffmpeg ./scripts/start_ch347_dirty_usb_x11.sh
```

这是历史兼容路径，主要用于对比，不是当前默认推荐方案。

## 当前已知限制与经验结论

- ST7796 的 `RAMWR` 是线性写入，不能在同一个窗口里跳过中间干净行
- 多矩形脏区在这条 CH347 链路上不划算，命令切换成本太高
- 真正瓶颈常常不是 SPI 时钟，而是 USB 事务和 GPIO/命令切换次数
- 触摸与显示是协作式共享总线，触摸活跃时会略微影响输出 FPS
- `60 MHz` 可能更快，但当前布线下比 `30 MHz` 更容易出现异常

项目里曾尝试过异步脏区计算，但稳定性差，现已去除。当前实现优先保证低延迟和可恢复性。

## 仓库里最值得先读的文件

- `scripts/start_ch347_dirty_usb_x11.sh`
- `scripts/ch347_dirty_usb_x11_daemon.sh`
- `src/xdamage_shm_capture.c`
- `src/ch347_dirty_usb_sink.c`
- `docs/X11_TOUCH.md`
- `docs/DRIVER.md`
- `docs/NOTES.md`

## 一句话总结

如果把这个项目当成一套系统来看，它做的是：

“在 Linux 上启动一个独立的 320x480 X11 桌面，利用 XDamage 捕获变化，把图像经 CH347 推到 SPI LCD，并可把 XPT2046 触摸再送回这个桌面。”
