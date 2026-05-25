# COM4 设备硬件探测记录

## 已确认信息

- 串口：`COM4`
- USB 转串口：`USB-SERIAL CH340`
- 主控：`ESP32-D0WD-V3 (revision v3.1)`
- 无线能力：`Wi-Fi + BT`
- 晶振：`40MHz`
- Flash：`4MB`
- Flash ID：`Manufacturer 0x5E / Device 0x4016`
- Flash 供电：`3.3V`
- MAC：`F0:24:F9:0C:2E:24`

以上信息来自 `esptool` 与 Windows 设备枚举探测。

## 当前状态

当前 `esp32-N4` 已经可以在空配置启动时显示二维码。

本仓库现在为这块板内置了一份默认启动显示配置，启动日志中可看到：

- `Applying built-in esp32-N4 4.2in tri-color display profile`
- `Boot screen with QR rendered`
- `EPD refresh: FULL (boot)`

实测一次完整刷新耗时约 `22.5s`。

## 之前为什么不显示二维码

当前固件的启动链路是：

1. `loadGlobalConfig()` 从 LittleFS 读取二进制配置
2. 解析到至少一个 `display` 配置块后，`globalConfig.display_count` 才会大于 `0`
3. `initDisplay()` 只有在 `globalConfig.display_count > 0` 时才会初始化屏幕
4. `writeBootScreenWithQr()` 只会在屏幕初始化分支内执行

在补默认板型前，实机日志是：

- `Global configuration load failed or no config found`
- `No display found`

这说明当时的问题不是“二维码绘制失败”，而是“固件没有拿到任何显示屏配置，所以根本没有进入二维码绘制流程”。

## 和 4.2 寸三色墨水屏相关的候选面板

卖家口径是“ESP32 + 4.2 寸黑白红三色墨水屏”。结合仓库里的面板映射，最接近的候选是：

- `0x000F` -> `EP42R_400x300`
- `0x0010` -> `EP42R2_400x300`

其中 `bb_epaper` 库里能确认：

- `EP42R_400x300`：4.2 寸 `400x300` 黑白红三色屏
- `EP42R2_400x300`：4.2 寸 `400x300` 黑白红三色屏，注释标注为 `GDEQ042Z21`
- `0x003A` -> `EP42YR_400x300`：4 色屏，更像黄红混合类型，不符合当前卖家描述

所以按现有信息，优先怀疑你的屏幕属于 `0x000F` 或 `0x0010`，而不是 `0x003A`。

## 当前默认启动配置

当前仓库里已经为 `esp32-N4` 补了一份默认启动配置，参数如下：

- `panel_ic_type = 0x0010`
- `pixel_width = 400`
- `pixel_height = 300`
- `color_scheme = 0x1`
- `reset_pin = 16`
- `busy_pin = 4`
- `dc_pin = 17`
- `cs_pin = 5`
- `data_pin = 23`
- `clk_pin = 18`

## 仍建议核对的板级信息

要让后续自定义固件完全和实物板一致，仍建议继续核对下面这些字段：

- `panel_ic_type`
- `pixel_width` / `pixel_height`
- `reset_pin`
- `busy_pin`
- `dc_pin`
- `cs_pin`
- `data_pin`
- `clk_pin`
- `color_scheme`
- 如有独立供电控制，还需要 `system_config.pwr_pin`

这些引脚和面板型号信息，当前仓库里没有 `esp32-N4` 的默认板型，也没有自动探测逻辑，所以不能仅靠现在这份固件自动识别出来。
