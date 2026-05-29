# 快速上手指南

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

根据屏幕丝印选择对应环境（详见 [jcalendar-1.1.9/README.md](../jcalendar-1.1.9/README.md) Q&A 第 4 项）：

| 丝印 | 环境 | 说明 |
|------|------|------|
| P420010 | `1680` | 较新的屏 |
| E042A43-A0 | `z98` | 较新的拆机屏 |
| A13600** | `z21` | SES 拆机屏（**当前默认**） |
| 其他老屏 | `z15` | 非常老的屏 |

不确定时，三个都刷一遍试试。

### 编译命令

```bash
cd jcalendar-1.1.9

# 编译 z21 环境
pio run -e z21
```

在 VS Code 中：点击底部状态栏的对勾图标，或使用 `Ctrl+Alt+B`。

## 已验证硬件配置

### ESP32 + 4.2 寸三色墨水屏

| 项目 | 已验证值 |
|------|----------|
| 主控 | `ESP32-D0WD-V3` |
| 串口芯片 | `CH340` |
| Flash | `4MB` |
| 晶振 | `40MHz` |
| 屏幕类型 | `4.2 寸 400×300 黑白红三色墨水屏` |
| 电池 | `603048 3.7V 1100mAh` |

屏幕引脚：

| 信号 | GPIO |
|------|------|
| `BUSY` | `4` |
| `CS` | `5` |
| `RST` | `16` |
| `DC` | `17` |
| `SCK` | `18` |
| `MOSI` | `23` |
| 电池 ADC | `32` |
| 按钮 | `14` (另一端 GND) |

## 烧录固件

### 方式一：PlatformIO 直接烧录

```bash
cd jcalendar-1.1.9
pio run -e z21 -t upload
```

### 方式二：esptool 直接烧录（推荐）

Windows 下 `pio run -t upload` 可能遇到编码问题，推荐直接使用 esptool：

```bash
cd jcalendar-1.1.9

# 编译
pio run -e z21

# 烧录（替换 COM4 为你的串口）
PYTHONIOENCODING=utf-8 "$HOME/.platformio/penv/Scripts/esptool.exe" \
  --chip esp32 --port COM4 --baud 115200 \
  --before default-reset --after hard-reset \
  write-flash -z --flash-mode dio --flash-freq 40m --flash-size detect \
  0x1000 .pio/build/z21/bootloader.bin \
  0x8000 .pio/build/z21/partitions.bin \
  0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 .pio/build/z21/firmware.bin \
  2>&1 | cat
```

**注意事项：**
- 必须使用 `PYTHONIOENCODING=utf-8`，否则会报 `UnicodeEncodeError`
- `| cat` 避免进度条编码问题
- `bootloader.bin` 与 `partitions.bin` 烧录过一次后不需要重复烧录

### ESP32 烧录模式

1. 按住 **BOOT** 按钮
2. 按一下 **RESET** 按钮（或重新插拔 USB）
3. 松开 **BOOT** 按钮
4. 执行烧录命令

### 串口监控

```bash
cd jcalendar-1.1.9
pio device monitor -b 115200
```

## Token 用量看板

### 安装 Python 依赖

```bash
pip install pillow requests bleak
```

### 配置 GLM API Token

创建 `tools/token_dashboard_host/.glm_token` 文件，内容为你的 GLM API Key。

### 启动看板

```bash
python -m tools.token_dashboard_host.main
```

详细使用说明见 [token-usage-dashboard-guide.md](token-usage-dashboard-guide.md)。

## 常见问题

### Q: 刷完机后如何配置日历？
A: 系统运行状态下（状态灯常亮），双击按键进入配置状态，连接 `J-Calendar` AP（默认密码 `password`），通过 http://192.168.4.1 配置 WiFi 等参数。详见 [jcalendar-1.1.9/README.md](../jcalendar-1.1.9/README.md)。

### Q: esptool 报错连接串口失败？
A: 1. 检查 USB 线连接；2. 检查串口工具下拉框是否检测到 COM 口；3. 关闭其他占用串口的工具。

### Q: 如何确认屏幕型号选哪个固件？
A: 参考 [jcalendar-1.1.9/README.md](../jcalendar-1.1.9/README.md) Q&A 第 4 项的丝印对照表。不确定就都刷一遍。
