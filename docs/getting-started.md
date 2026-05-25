# Open Display Firmware 使用指南

## 环境准备

### 1. 安装 PlatformIO

1. 安装 [VS Code](https://code.visualstudio.com/)
2. 在 VS Code 中安装 **PlatformIO IDE** 扩展

或通过命令行安装 PlatformIO Core：
```bash
pip install platformio
```

### 2. 克隆项目

```bash
git clone <repository-url>
cd AI-Usage
```

## 编译固件

### 选择目标环境

根据你的硬件，选择对应的 PlatformIO 环境：

| 开发板 | 环境名 |
|--------|--------|
| ESP32-S3 (16MB Flash + 8MB PSRAM) | `esp32-s3-N16R8` |
| ESP32-S3 (8MB Flash + 8MB PSRAM) | `esp32-s3-N8R8` |
| ESP32-S3 (32MB Flash + 8MB PSRAM) | `esp32-s3-N32R8` |
| ESP32-C3 (4MB Flash) | `esp32-c3-N4` |
| ESP32-C6 (4MB Flash) | `esp32-c6-N4` |
| ESP32 经典版 (4MB Flash) | `esp32-N4` |
| NRF52840 (Seeed XIAO) | `nrf52840custom` |

### 编译命令

```bash
# 编译 ESP32-S3 16MB 版本
pio run -e esp32-s3-N16R8

# 编译 ESP32-C3 版本
pio run -e esp32-c3-N4

# 编译 NRF52840 版本
pio run -e nrf52840custom
```

在 VS Code 中：点击底部状态栏的对勾图标，或使用 `Ctrl+Alt+B`。

## 已验证硬件配置

### `esp32-N4` + 4.2 寸三色墨水屏

当前仓库已经按下面这套硬件组合完成过实机验证，可正常启动、显示二维码并输出 BLE 日志：

| 项目 | 已验证值 |
|------|----------|
| 主控 | `ESP32-D0WD-V3` |
| 串口芯片 | `CH340` |
| Flash | `4MB` |
| 晶振 | `40MHz` |
| 屏幕类型 | `4.2 寸 400x300 黑白红三色墨水屏` |
| 默认面板配置 | `panel_ic_type = 0x0010 (EP42R2_400x300)` |
| 默认色彩方案 | `color_scheme = 0x1` |
| 电池 | `603048 3.7V 1100mAh` |

默认启动屏幕引脚如下：

| 信号 | GPIO |
|------|------|
| `BUSY` | `4` |
| `CS` | `5` |
| `RST` | `16` |
| `DC` | `17` |
| `SCK` | `18` |
| `MOSI` | `23` |

说明：

- 当前内置默认配置只针对 `esp32-N4` 生效
- `pwr_pin` 当前未配置，启动日志中会看到 `Power pin not set`
- 更完整的探测与排障记录见 [esp32-com4-hardware-notes.md](d:/open-sprout/AI-Usage/AI-Usage/docs/esp32-com4-hardware-notes.md)

## 烧录固件

### USB 烧录 (推荐)

```bash
# 烧录并监控串口
pio run -e esp32-s3-N16R8 -t upload -t monitor

# 仅烧录
pio run -e esp32-s3-N16R8 -t upload
```

### ESP32 经典版 (`esp32-N4`) 实测烧录命令

如果你使用的是经典 ESP32 开发板，推荐先单独编译：

```bash
pio run -e esp32-N4
```

在 Windows 上，`pio run -t upload` 可能会受到终端编码或 `esptool 5.2.0` 参数兼容问题影响。遇到这类情况时，可直接使用 `esptool` 写入：

```powershell
$env:PYTHONIOENCODING='utf-8'
& "$env:USERPROFILE\.platformio\penv\Scripts\esptool.exe" `
  --chip esp32 -p COM4 -b 460800 `
  --before default_reset --after hard_reset `
  write-flash --flash-mode dio --flash-freq 40m --flash-size 4MB --no-progress `
  0x1000 .pio/build/esp32-N4/bootloader.bin `
  0x8000 .pio/build/esp32-N4/partitions.bin `
  0xe000 C:/Users/<用户名>/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin `
  0x10000 .pio/build/esp32-N4/firmware.bin
```

说明：

- `COM4` 需要替换成你自己的串口号
- `boot_app0.bin` 路径来自 PlatformIO 安装目录
- 当前仓库已验证 `esp32-N4 + COM4` 可以通过这条命令正常烧录

### ESP32 烧录模式

1. 按住 **BOOT** 按钮
2. 按一下 **RESET** 按钮 (或重新插拔 USB)
3. 松开 **BOOT** 按钮
4. 执行烧录命令

> 使用 USB-CDC 的 ESP32-S3 板子 (如 `ARDUINO_USB_CDC_ON_BOOT=1`) 通常不需要手动进入烧录模式。

### 串口监控

```bash
pio device monitor -b 115200
```

在 VS Code 中：点击底部插头图标，或使用 `Ctrl+Alt+S`。

### Windows 下抓取一次完整启动日志

如果只想抓开机日志，不想长期占用串口，可以直接执行：

```powershell
$port = New-Object System.IO.Ports.SerialPort 'COM4',115200,'None',8,'one'
$port.ReadTimeout = 500
$port.DtrEnable = $false
$port.RtsEnable = $true
$port.Open()
Start-Sleep -Milliseconds 200
$port.RtsEnable = $false
Start-Sleep -Milliseconds 200
$port.DiscardInBuffer()
$deadline = (Get-Date).AddSeconds(20)
while ((Get-Date) -lt $deadline) {
  try {
    $line = $port.ReadLine()
    Write-Output $line
  } catch [System.TimeoutException] {}
}
$port.Close()
```

这段脚本会：

1. 打开 `COM4`
2. 通过 `RTS` 触发一次复位
3. 连续读取约 20 秒启动日志
4. 自动释放串口

常见成功标志：

- `Applying built-in esp32-N4 4.2in tri-color display profile`
- `Boot screen with QR rendered`
- `EPD refresh: FULL (boot)`
- `=== BLE advertising started successfully ===`
- `=== Setup completed successfully ===`

如果仍然看到下面这些日志，说明还没有真正进入点屏流程：

- `Global configuration load failed or no config found`
- `No display found`

## 首次配置

### 方式一：使用 Web 工具 (推荐)

1. 烧录固件后，如设备已写入 `display` 配置块，或固件内置了对应板型的默认屏幕配置，设备会显示包含二维码的启动画面
2. 扫描二维码打开配置页面
3. 通过 BLE 连接设备 (设备名: `OD<芯片ID>`)
4. 在 Web 页面中配置设备参数并写入

官方配置工具：https://opendisplay.org/firmware/config/

### 方式二：使用 Open Display 工具箱

1. 从 https://opendisplay.org 下载工具箱应用
2. 通过 BLE 连接设备
3. 配置显示屏、传感器、WiFi 等参数
4. 将配置写入设备

### 方式三：自定义 BLE 客户端

通过 BLE UUID `00002446-0000-1000-8000-00805F9B34FB` 连接，发送二进制命令。

## 配置流程详解

### 1. 连接设备

设备启动后通过 BLE 广播，广播名称格式为 `OD` + 芯片 ID 的十六进制。广播数据包含 Manufacturer Specific Data (MSD)，其中携带温度、电池电压、按钮状态等信息。

### 2. 写入配置

配置以二进制 TLV 格式传输。完整的配置至少需要以下包：

- `0x01` system_config：IC 类型、通信模式、电源引脚
- `0x02` manufacturer_data：制造商信息
- `0x04` power_option：电源模式、电池参数、睡眠设置
- `0x20` display：显示面板类型、引脚、分辨率

如果使用 WiFi，还需要：
- `0x24` data_bus：I2C 总线配置 (如果有传感器)
- `0x26` wifi_config：SSID 和密码

### 3. 配置示例 (伪代码)

```python
# 连接 BLE 设备
device = ble.connect("ODXXXXXXXX")

# 写入配置 (简化示例)
config = build_tlv_config([
    (0x01, system_config),    # 系统配置
    (0x02, manufacturer_data), # 制造商数据
    (0x04, power_option),      # 电源选项
    (0x20, display_config),    # 显示屏配置
])

# 发送写入命令 (0x0041)
device.write_characteristic(0x2446, b'\x00\x41' + config)
```

## 发送图像

### 直接写入模式

这是主要的图像传输方式，流程如下：

```
客户端                               设备
  │                                    │
  │── DIRECT_WRITE_START (0x0070) ────>│
  │<── ACK (0x0070) ──────────────────│
  │                                    │
  │── DIRECT_WRITE_DATA (0x0071) ─────>│  (重复多次)
  │<── ACK (0x0071) ──────────────────│
  │   ...                              │
  │                                    │
  │── DIRECT_WRITE_END (0x0072) ──────>│
  │<── ACK (0x0072) ──────────────────│
  │                                    │
  │   [设备刷新屏幕...]                │
  │                                    │
  │<── REFRESH_SUCCESS (0x0073) ──────│
  │   或 REFRESH_TIMEOUT (0x0074)      │
```

### 局部更新模式

用于更新屏幕的部分区域，需要匹配 etag：

```
客户端                                 设备
  │                                      │
  │── PARTIAL_WRITE_START (0x0076) ────>│
  │    flags + old_etag + new_etag       │
  │    + x, y, width, height             │
  │<── ACK (0x0076) ────────────────────│
  │                                      │
  │── DIRECT_WRITE_DATA (0x0071) ──────>│  (old_plane + new_plane)
  │<── ACK (0x0071) ────────────────────│
  │   ...                                │
  │                                      │
  │── DIRECT_WRITE_END (0x0072) ───────>│
  │<── ACK (0x0072) ────────────────────│
  │   [设备局部刷新]                     │
  │<── REFRESH_SUCCESS (0x0073) ────────│
```

### 压缩传输

在 DIRECT_WRITE_START 中附带 4 字节的解压后大小，设备自动使用 zlib 解压。需要 display 配置中启用 `TRANSMISSION_MODE_ZIP` 或 `TRANSMISSION_MODE_ZIPXL`。

## WiFi 局域网传输

### 配置步骤

1. 在设备配置中启用 WiFi 通信模式 (communication_modes bit 2)
2. 写入 `wifi_config` (0x26) 包含 SSID 和密码
3. 设备启动后自动连接 WiFi
4. 设备通过 mDNS 广播服务 `_opendisplay._tcp`，域名格式 `OD<芯片ID>.local`
5. TCP 服务器监听端口 2446

### TCP 帧格式

```
[2字节小端长度] [payload]
```

Payload 与 BLE 命令格式完全相同 (2 字节命令码 + 数据)。

### 使用示例

```python
import socket

# 连接设备
sock = socket.socket()
sock.connect(("ODXXXXXXXX.local", 2446))

# 发送读取固件版本命令
payload = bytes([0x00, 0x43])  # FIRMWARE_VERSION
frame = bytes([len(payload) & 0xFF, (len(payload) >> 8) & 0xFF]) + payload
sock.send(frame)

# 接收响应
header = sock.recv(2)
resp_len = header[0] | (header[1] << 8)
response = sock.recv(resp_len)
```

## 电源管理配置

### 电池供电模式

设置 `power_option.power_mode = 1` 启用电池模式：

1. 设备启动后显示启动画面
2. 无 BLE/WiFi 活动时进入深度睡眠
3. 配置 `deep_sleep_time_seconds` 控制睡眠时间
4. 唤醒后最小化启动，仅 BLE 广播
5. 在 `sleep_timeout_ms` 时间内等待连接
6. 如有连接则加载完整功能，否则重新进入深度睡眠
7. 首次启动有 60 秒保护期，不会进入深度睡眠

### USB 供电模式

设置 `power_option.power_mode = 0` (或非 1)，设备保持常开状态。

## 串口日志

所有平台通过串口输出调试信息，波特率 115200。

ESP32-S3 extuart 环境支持通过外接 UART (RX=GPIO44, TX=GPIO43) 输出日志，不影响 USB CDC 通信。

## 常见问题

### Q: 设备名是什么？
A: `OD` + 芯片唯一 ID 的十六进制，如 `OD1A2B3C4D5E`。

### Q: 支持同时 BLE 和 WiFi 吗？
A: 支持。ESP32 平台可以同时启用 BLE 和 WiFi，响应会发送到两个通道。

### Q: 如何恢复出厂配置？
A: 通过 BLE 发送写入配置命令 (0x0041) 写入新的配置。如果启用了加密且忘记了密钥，可以使用安全配置中的 `rewrite_allowed` 标志或硬件复位引脚。

### Q: 图像传输支持多大的图片？
A: 取决于设备内存。ESP32-S3 带 PSRAM 支持最大 512KB 压缩数据，无 PSRAM 的设备支持约 54KB。

### Q: 局部更新有什么限制？
A: 仅支持 1bpp (黑白) 面板，矩形区域必须 8 像素对齐，需要匹配之前全量刷新的 etag。
