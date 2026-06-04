# AI-Usage 项目快照

## 项目概述

Token 用量看板 —— 基于 ESP32 + 4.2寸 400×300 黑白红三色墨水屏的 AI 用量监控显示系统。

主机端 Python 程序定时采集 AI 服务用量数据，通过 BLE 推送到 ESP32 设备，设备在墨水屏上渲染看板页面。

固件基于 **J-Calendar** (jcalendar-1.1.9) `z21` 环境，内嵌看板渲染模块。

---

## 硬件配置

### 主控
- **芯片**: ESP32-D0WD-V3 (revision v3.1)
- **Flash**: 4MB (Manufacturer 0x5E / Device 0x4016)
- **串口**: COM4 (USB-SERIAL CH340)
- **无线**: Wi-Fi + BLE
- **MAC**: F0:24:F9:0C:2E:24

### 墨水屏接线 (SES 拆机屏，丝印 A13600**)

| 信号 | GPIO |
|------|------|
| BUSY | 4 |
| CS | 5 |
| RST | 16 |
| DC | 17 |
| SCK | 18 |
| MOSI | 23 |
| 电池 ADC | 32 |
| 按钮 | 14 (另一端 GND) |

### 固件参数

| 项目 | 值 |
|------|-----|
| 固件目录 | `jcalendar-1.1.9/` |
| 编译环境 | `z21` |
| 分辨率 | 400 × 300 |
| BLE UUID | `00002446-0000-1000-8000-00805F9B34FB` |

---

## 刷机命令

### 编译
```bash
cd jcalendar-1.1.9
pio run -e z21
```

### 烧录 (esptool，推荐避免编码问题)
```bash
PYTHONIOENCODING=utf-8 "$HOME/.platformio/penv/Scripts/esptool.exe" \
  --chip esp32 --port COM4 --baud 115200 \
  --before default-reset --after hard-reset \
  write_flash -z --flash-mode dio --flash-freq 40m --flash-size detect \
  0x1000 .pio/build/z21/bootloader.bin \
  0x8000 .pio/build/z21/partitions.bin \
  0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 .pio/build/z21/firmware.bin \
  2>&1 | cat
```

> 替换 `COM4` 为实际串口。Windows 下 `~` 可能无效，直接用完整路径。

---

## 常用操作

### 启动看板
```bash
python -m tools.token_dashboard_host.main
```

### 停止看板
```powershell
Get-Process python | Stop-Process -Force
```

### 清除缓存
```powershell
del .dashboard_cache.json
```

### 设置开机自启动
```powershell
cd tools\token_dashboard_host
.\setup_autostart.ps1
```

### 取消自启动
```powershell
.\remove_autostart.ps1
```

### 测试数据源 API
```bash
curl http://localhost:3847/api/summary?range=day
curl http://localhost:3847/api/models?range=day
```

---

## 文件结构速查

```
jcalendar-1.1.9/src/
├── main.cpp              # 入口
├── app_config.h          # 引脚、BLE 常量
├── display_service.cpp    # 显示驱动、快刷
├── dashboard_protocol.cpp # 看板协议状态机
├── dashboard_renderer.cpp # 看板渲染
└── ble_service.cpp        # BLE 服务

tools/token_dashboard_host/
├── main.py               # 主入口
├── config.py             # 配置（渲染模式、刷新间隔）
├── collectors/           # 数据采集（AIUsage/GLM/GPT）
└── ble/transport.py      # BLE 传输层
```

---

## 故障排查

| 问题 | 解决方案 |
|------|----------|
| 数据为空 | 停止旧进程 `Get-Process python \| Stop-Process -Force` |
| GLM 数据为空 | 检查 `tools/token_dashboard_host/.glm_token` |
| 设备连接失败 | 按 RST 重启，检查 BLE 设备名是否以 `OD` 开头 |
| 串口占用 | 关闭其他串口工具或更换 COM 口 |
| 编码问题烧录失败 | 使用 `PYTHONIOENCODING=utf-8` 前缀 |

---

## 参考文档

- [Token 用量看板完整文档](docs/token-usage-dashboard-guide.md)
- [固件概述](docs/firmware-overview.md)
- [硬件详情](docs/esp32-com4-hardware-notes.md)
- [J-Calendar README](jcalendar-1.1.9/README.md) — 按钮操作、Web 配置、Q&A
