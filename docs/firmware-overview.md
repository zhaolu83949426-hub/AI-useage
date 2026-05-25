# Open Display Firmware 功能概述

## 项目简介

Open Display Firmware 是一个基于 BLE (低功耗蓝牙) 的电子墨水屏标签固件，支持 NRF52840 和 ESP32 系列芯片。设备通过 BLE 或 WiFi 接收图像数据，驱动电子墨水屏显示内容。

官方资源：
- 安装指南：https://opendisplay.org/firmware/install/
- 配置指南：https://opendisplay.org/firmware/config/
- 显示测试：https://opendisplay.org/firmware/display/
- 社区 Discord：https://discord.gg/wgQ8XsgMkv

## 支持的硬件平台

| 平台 | 环境名称 | Flash | PSRAM | 说明 |
|------|----------|-------|-------|------|
| ESP32-S3 | `esp32-s3-N16R8` | 16MB | 8MB | 推荐配置 |
| ESP32-S3 | `esp32-s3-N8R8` | 8MB | 8MB | |
| ESP32-S3 | `esp32-s3-N32R8` | 32MB | 8MB | 大容量 |
| ESP32-S3 | `esp32-s3-N16R8-extuart` | 16MB | 8MB | 外接日志 UART (RX=44, TX=43) |
| ESP32-C3 | `esp32-c3-N4` | 4MB | 无 | 精简版 |
| ESP32-C6 | `esp32-c6-N4` | 4MB | 无 | 精简版 |
| ESP32 (经典) | `esp32-N4` | 4MB | 无 | 精简版，当前已验证 `4.2 寸 400x300 黑白红三色墨水屏` 默认启动配置 |
| NRF52840 | `nrf52840custom` | - | - | Seeed XIAO BLE Sense |

### `esp32-N4` 已验证默认硬件配置

当前仓库内置了一份仅对 `esp32-N4` 生效的默认启动显示配置，用于经典 ESP32 + 4.2 寸三色墨水屏组合：

| 配置项 | 值 |
|--------|----|
| `panel_ic_type` | `0x0010 (EP42R2_400x300)` |
| `pixel_width` | `400` |
| `pixel_height` | `300` |
| `color_scheme` | `0x1` |
| `busy_pin` | `4` |
| `cs_pin` | `5` |
| `reset_pin` | `16` |
| `dc_pin` | `17` |
| `clk_pin` | `18` |
| `data_pin` | `23` |

这套配置已经实机验证可以完成：

- 开机显示二维码
- 全量刷新电子纸
- 启动 BLE 广播

## 核心功能模块

### 1. 通信方式

固件支持三种通信模式，通过配置中的 `communication_modes` 位域控制：

| 模式 | 位 | 说明 |
|------|----|------|
| BLE | bit 0 | 蓝牙低功耗传输 (UUID: 0x2446) |
| OEPL | bit 1 | OEPL 协议传输 |
| WiFi | bit 2 | WiFi STA + TCP 局域网传输 |

- **BLE**：所有平台均支持，MTU 512 字节，设备名称格式 `OD<芯片ID>`
- **WiFi**：仅 ESP32 平台，连接路由器后在端口 2446 开启 TCP 服务器，支持 mDNS 发现 (`_opendisplay._tcp`)
- BLE 和 WiFi 可同时工作，响应会同时发送到两个通道

### 2. 显示驱动

支持两种驱动路径：

- **bb_epaper 库**：支持 60+ 种电子墨水屏面板 (通过 `panel_ic_type` ID 映射)
- **Seeed_GFX (TFT_eSPI)**：专门支持 Seeed ED103TC2 面板 (1872x1404)，包括 1bpp 和 4bpp 灰度模式

支持的图像传输模式 (transmission_modes 位域)：

| 模式 | 位 | 说明 |
|------|----|------|
| ZIPXL | bit 0 | 扩展压缩传输 (大内存设备) |
| ZIP | bit 1 | zlib 压缩传输 |
| G5 | bit 2 | G5 传输 |
| DIRECT_WRITE | bit 3 | 直接写入模式 |
| CLEAR_ON_BOOT | bit 7 | 开机清屏 (跳过启动画面) |

显示操作类型：
- **全量刷新 (Full)**：完整刷新屏幕
- **快速刷新 (Fast/Partial)**：部分刷新，速度更快
- **局部更新 (Partial Write)**：仅更新指定矩形区域，基于 etag 版本控制

支持的色彩方案 (color_scheme)：
- 0: 黑白 1bpp
- 1/2: 黑白红/黑白黄 双色 (双平面)
- 3: 2bpp 灰度
- 4: 4bpp 灰度
- 5: 2bpp 灰度 (编码方式不同)
- 6 (GRAY16): 16 级灰度

### 3. 配置系统

设备通过 BLE/WiFi 接收 TLV 格式的二进制配置数据包，存储在 LittleFS 文件系统中。

配置包类型：

| 包类型 | ID | 说明 | 数量 |
|--------|----|------|------|
| system_config | 0x01 | 系统配置 (IC类型、通信模式、电源引脚) | 1 |
| manufacturer_data | 0x02 | 制造商数据 | 1 |
| power_option | 0x04 | 电源选项 (电池、睡眠、发射功率) | 1 |
| display | 0x20 | 显示屏配置 (引脚、分辨率、面板类型) | 最多 4 |
| led | 0x21 | LED 配置 (RGB 引脚、类型) | 最多 4 |
| sensor_data | 0x23 | 传感器配置 | 最多 4 |
| data_bus | 0x24 | 数据总线 (I2C/SPI) | 最多 4 |
| binary_inputs | 0x25 | 按钮输入配置 | 最多 4 |
| wifi_config | 0x26 | WiFi SSID 和密码 | 1 |
| security_config | 0x27 | 安全配置 (加密密钥) | 1 |
| touch_controller | 0x28 | 触摸屏控制器 (GT911) | 最多 4 |
| passive_buzzer | 0x29 | 无源蜂鸣器配置 | 最多 4 |

配置写入支持分块传输 (最大 200 字节/块，最多 20 块)。

### 4. 电源管理

- **电池供电模式** (power_mode = 1)：支持深度睡眠，可配置睡眠时间
- **深度睡眠唤醒**：ESP32 使用 RTC 定时器唤醒，唤醒后进入最小化模式 (仅 BLE 广播)，等待连接后加载完整功能
- **首次启动延迟**：首次启动后等待 60 秒才允许进入深度睡眠
- **广播超时**：深度睡眠唤醒后，如果在配置的超时时间内无 BLE 连接，自动回到深度睡眠
- **AXP2101 PMIC 支持**：完整的电源管理 IC 控制，包括电池电压、充电状态、各路 LDO/DCDC 开关
- **外部 Flash 关断**：NRF52840 上关闭外部 SPI Flash 以省电

### 5. 传感器支持

| 传感器 | 类型 ID | 说明 |
|--------|---------|------|
| 温度 | 0x0001 | 芯片内部温度传感器 |
| 湿度 | 0x0002 | 湿度传感器 |
| AXP2101 | 0x0003 | 电源管理 IC (电池电压、充电状态等) |
| SHT40 | 0x0004 | 温湿度传感器 (I2C, 默认地址 0x44) |

传感器数据通过 BLE Manufacturer Specific Data (MSD) 广播发送，包含：
- 芯片温度 (编码: (温度 + 40) * 2)
- 电池电压
- 设备状态字节 (电池电压高位、重启标志、连接请求、循环计数)
- 按钮状态
- SHT40 温湿度数据

### 6. 输入设备

**按钮 (Binary Inputs)**：
- 最多 32 个按钮 (4 实例 x 8 引脚)
- 支持中断模式检测
- 按下计数和状态跟踪
- 支持引脚反转和上下拉配置

**触摸屏 (Touch Controller)**：
- 支持 GT911 触摸 IC (I2C 地址 0x5D 或 0x14)
- 支持坐标翻转和 XY 交换
- 最多 5 个触摸点
- EPD 刷新期间自动挂起触摸轮询

### 7. LED 控制

- 最多 4 个 LED 实例
- 支持 RGB 三色、单色、红黄双色等类型
- 引脚电平反转支持
- 开机彩虹闪烁指示

### 8. 蜂鸣器

- 最多 4 个无源蜂鸣器实例
- PWM 驱动，频率范围 400Hz - 12000Hz
- 支持音符序列播放 (索引映射频率)
- 可选使能引脚

### 9. 加密与安全

- AES-128-CCM 加密
- 会话认证机制
- Nonce 重放防护
- 会话超时
- 可选硬件复位引脚 (支持极性和上下拉配置)
- DFU 模式入口 (加密启用时禁用 BLE DFU)

### 10. 启动画面

- 启动时显示包含设备信息和二维码的欢迎画面
- 二维码内容为 Open Display 配置页面 URL + 设备芯片 ID
- 显示固件版本号
- 可通过 `CLEAR_ON_BOOT` 传输模式标志跳过

## BLE 命令协议

所有命令通过 BLE 特征值 (UUID: 0x2446) 传输，命令格式为 2 字节命令码 + 可选数据。

| 命令码 | 名称 | 说明 |
|--------|------|------|
| 0x0040 | READ_CONFIG | 读取设备配置 (分块返回) |
| 0x0041 | WRITE_CONFIG | 写入配置 (支持分块) |
| 0x0042 | WRITE_CONFIG_CHUNK | 配置分块续传 |
| 0x0043 | FIRMWARE_VERSION | 获取固件版本和 Git SHA |
| 0x0044 | READ_MSD | 读取 Manufacturer Specific Data |
| 0x0050 | AUTHENTICATE | 加密认证 |
| 0x0051 | ENTER_DFU | 进入 DFU 模式 |
| 0x0070 | DIRECT_WRITE_START | 开始直接写入图像 |
| 0x0071 | DIRECT_WRITE_DATA | 传输图像数据 |
| 0x0072 | DIRECT_WRITE_END | 结束写入并刷新显示 |
| 0x0073 | LED_ACTIVATE | LED 激活命令 |
| 0x0075 | BUZZER_ACTIVATE | 蜂鸣器激活命令 |
| 0x0076 | PARTIAL_WRITE_START | 开始局部更新 |
| 0x000F | REBOOT | 重启设备 |

响应格式：
- 第 1 字节：`0x00` = 成功，`0xFF` = 错误
- 第 2 字节：命令码低字节
- 后续字节：数据或错误码

## 项目文件结构

```
├── src/                          # 源代码
│   ├── main.cpp / main.h         # 入口、setup/loop、电源管理
│   ├── structs.h                 # 所有数据结构定义
│   ├── communication.cpp/h       # BLE 命令处理和响应
│   ├── display_service.cpp/h     # 显示驱动、图像传输、传感器初始化
│   ├── display_seeed_gfx.cpp/h   # Seeed ED103 面板专用驱动
│   ├── ble_init.cpp/h            # BLE 初始化和广播
│   ├── wifi_service.cpp/h        # WiFi STA/TCP 服务
│   ├── config_parser.cpp/h       # 配置 TLV 解析和持久化
│   ├── encryption.cpp/h          # AES-128-CCM 加密
│   ├── encryption_state.h        # 加密会话状态
│   ├── device_control.cpp/h      # 按钮、LED、重启、DFU
│   ├── touch_input.cpp/h         # GT911 触摸屏驱动
│   ├── boot_screen.cpp/h         # 启动画面和二维码
│   ├── buzzer_control.cpp/h      # 无源蜂鸣器控制
│   ├── sensor_sht40.cpp/h        # SHT40 温湿度传感器
│   ├── driver.h                  # bb_epaper 引擎声明
│   ├── esp32_ble_callbacks.h     # ESP32 BLE 回调 (命令/响应队列)
│   └── qr/                       # 二维码生成库
├── lib/
│   └── Seeed_GFX/                # TFT_eSPI 修改版 (Seeed 电子墨水屏)
├── boards/                       # 自定义板定义
├── variants/                     # NRF52840 引脚变体
├── bin/                          # NRF52840 bootloader hex
├── docs/                         # 文档
└── platformio.ini                # PlatformIO 构建配置
```
