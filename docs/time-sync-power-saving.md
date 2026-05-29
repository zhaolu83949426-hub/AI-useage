# 时间同步省电唤醒机制

## 概述

Token Usage Dashboard 设备通过时间同步实现智能唤醒，在保持数据及时更新的同时最大化电池续航。

## 设计方案

### 核心机制

- 设备在每小时的 **0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55** 分醒来广播 BLE，持续 **30 秒**
- 脚本在整点 **5 秒后**连接推送（如 0:05, 5:05, 10:05...），确保设备已醒来
- **0:00-8:00** 设备进入夜间深度睡眠，不唤醒
- 数据传输并刷新完成后，设备立即回睡眠，不等待下一个周期

### 时序示意图

```
时间轴：
09:00  设备醒来，广播 30 秒
09:00:05 脚本连接推送 (醒着)
09:00:35 传输完成，刷新完成 → 立即睡眠

09:05  设备醒来，广播 30 秒
09:05:05 脚本连接推送 (醒着)
09:05:35 刷新完成 → 立即睡眠

00:00  夜间模式，直接睡到 08:00
08:00  早上 8 点恢复正常周期
```

## 协议扩展

### DashboardSnapshotV1 二进制格式

使用 192 字节 payload 的末尾 3 字节（189-191）作为时间同步数据：

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 189 | sync_hour | uint8 | 同步小时 (0-23) |
| 190 | sync_minute | uint8 | 同步分钟 (0-59) |
| 191 | sync_flags | uint8 | 标志位（保留） |

## 固件实现

固件源码位于 `jcalendar-1.1.9/src/`。

### 配置

固件内嵌省电配置，在电池供电模式下自动启用时间同步唤醒。

### 渲染后睡眠

`dashboard_renderer.cpp` 中渲染完成后，如果时间同步模式激活，设备立即进入深度睡眠。

## 脚本实现

### 整点对齐 (`tools/token_dashboard_host/main.py`)

脚本计算下一个 5 分钟整点 +5 秒的时间，等待到该时刻再执行采集和推送。

### 时间编码 (`tools/token_dashboard_host/renderer/firmware_render.py`)

DashboardSnapshotV1 的末尾 3 字节编码当前时间，供设备同步时钟。

## 省电效果

### 当前模式 (24/7 常开)
- BLE 持续广播：~15mA
- 24 小时消耗：~360mAh

### 时间同步模式
- 醒 30 秒 / 5 分钟：占 10%
- 平均电流：~1.5mA
- 24 小时消耗：~36mAh
- **夜间 12 小时几乎全睡**：再省 ~18mAh

**预计节省：~85% 电量**

## 烧录命令

```bash
cd jcalendar-1.1.9
pio run -e z21

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

## 验证方法

1. 脚本日志应显示：
   ```
   Next update at XX:05:05 (waiting XXXs)
   ```

2. 固件串口日志应显示：
   ```
   Time sync mode enabled (battery power saving)
   Dashboard render complete - entering deep sleep
   Night mode: sleeping until 08:00
   ```

3. 屏幕正常刷新，数据正确显示

## 相关文件

### 固件 (`jcalendar-1.1.9/src/`)

- `dashboard_renderer.cpp/h` — 时间解析、渲染后睡眠
- `main.cpp` — 智能唤醒、夜间睡眠

### 脚本 (`tools/token_dashboard_host/`)

- `renderer/firmware_render.py` — 时间编码
- `main.py` — 整点对齐
