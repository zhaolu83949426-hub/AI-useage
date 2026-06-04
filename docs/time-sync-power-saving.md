# 时间同步省电唤醒机制

## 概述

Token Usage Dashboard 设备通过时间同步实现智能唤醒，在保持数据及时更新的同时最大化电池续航。设备在电池供电时自动启用省电模式，USB 充电时正常运行。

## 配置参数

| 参数 | 值 | 来源 |
|------|-----|------|
| 唤醒间隔 | 5 分钟 | `app_config.h` → `kWakeIntervalMin` |
| 唤醒窗口 | 15 秒 | `app_config.h` → `kWakeWindowSec` |
| 夜间模式 | 0:00 - 8:00 | `app_config.h` → `kNightStartHour` / `kNightEndHour` |
| 单次最大深睡眠 | 1 小时 | `app_config.h` → `kMaxDeepSleepSec` |
| 电池供电阈值 | < 4200mV | `app_config.h` → `kBatteryThresholdMv` |
| RTC 同步超时 | 3 天 | `main.cpp` → `g_rtcLastSyncTime` |

## 工作流程

### 启动决策流程

```
设备启动 (setup)
  │
  ├─ 定时唤醒 + 之前在省电模式 + 仍在夜间?
  │   └─ YES → 跳过所有初始化，直接回深睡眠（快速路径）
  │
  ├─ 初始化 SPI / BLE / Display
  │
  ├─ 检测电池电压 < 4.2V (电池供电)?
  │   ├─ NO → 正常模式（不睡眠，持续运行）
  │   └─ YES → 检查 RTC 状态
  │       ├─ RTC 年份 < 2024（未初始化）→ 保持唤醒，等待首次推送同步
  │       ├─ 距上次同步 > 3 天 → 保持唤醒，等待重新同步
  │       └─ RTC 有效且同步未过期 → 进入省电模式
  │
  └─ 进入主循环 (loop)
```

### 白天省电模式（8:00 - 0:00）

```
时间轴 (每 5 分钟一个周期):

08:00:00  设备醒来，初始化 SPI/BLE/Display
08:00:00  开始 BLE 广播
08:00:05  脚本连接推送 (在 15s 窗口内)
08:00:20  数据传输完成 → 渲染 → 刷新 → 立即深睡眠

08:05:00  下一次唤醒，重复上述周期
  ...
```

- 若脚本未连接，15 秒唤醒窗口到期后自动回深睡眠
- 数据到达后同步 RTC（每次推送都同步，修正 RTC 漂移）

### 夜间模式（0:00 - 8:00）

ESP32 内部 RC 振荡器精度约 ±5%，为限制漂移累积，夜间分段睡眠：

```
00:00  深睡眠 1 小时（最长 kMaxDeepSleepSec）
01:00  定时唤醒 → 检测仍在夜间 → 跳过初始化直接回睡（快速路径，<100ms）
02:00  定时唤醒 → 继续睡
  ...
07:00  定时唤醒 → 继续睡
08:00  定时唤醒 → 已是白天 → 完整初始化 → 正常省电周期
```

- 快速路径不初始化 SPI / BLE / Display，功耗可忽略
- 单次最长睡眠 1 小时 → 最大漂移 ±3 分钟（15s 窗口可覆盖）

### RTC 同步保护

| 条件 | 行为 | 原因 |
|------|------|------|
| RTC 未初始化（年份 < 2024） | 保持唤醒，不进入省电模式 | 刷固件后 RTC 是 1970 年，与脚本时间永远对不上 |
| 距上次同步 > 3 天 | 保持唤醒，不进入省电模式 | 漂移过大，可能无法与脚本对齐 |
| 首次推送到达 | 同步 RTC + 激活省电模式 | 从"保持唤醒"自动切换到"省电模式" |
| 后续每次推送 | 同步 RTC | 持续修正漂移，保持与脚本对齐 |

同步时间戳通过 `RTC_DATA_ATTR` 跨深睡眠保留。

## 时序示意图

```
白天正常周期:
09:00:00  唤醒 → BLE 广播
09:00:05  脚本连接推送
09:00:15  传输完成 → 渲染 → 刷新 → 深睡眠
          |<--- ~15s 活跃 --->|

空闲周期（无推送）:
09:05:00  唤醒 → BLE 广播
09:05:15  窗口超时 → 深睡眠
          |<-- 15s 活跃 -->|

夜间模式:
00:00     深睡眠 1h
01:00     唤醒 → 快速检测 → 继续睡 1h (<100ms)
02:00     唤醒 → 快速检测 → 继续睡 1h
  ...     (重复)
07:00     唤醒 → 快速检测 → 继续睡 1h
08:00     唤醒 → 白天 → 完整初始化 → 正常周期
```

## 协议扩展

### DashboardSnapshotV1 二进制格式

使用 192 字节 payload 的末尾 3 字节（189-191）作为时间同步数据：

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 189 | sync_hour | uint8 | 同步小时 (0-23) |
| 190 | sync_minute | uint8 | 同步分钟 (0-59) |
| 191 | sync_flags | uint8 | 标志位（保留） |

## 省电效果估算

### 典型电流

| 状态 | 电流 | 说明 |
|------|------|------|
| 深度睡眠 | ~15-50µA | ESP32 RTC 定时器唤醒 |
| 活跃（BLE 广播） | ~40-80mA | CPU + BLE 协议栈 |
| 墨水屏刷新 | ~20-40mA | UC8276 刷新瞬间，静态 0µA |
| 夜间快速唤醒 | ~数 mA | <100ms，可忽略 |

### 日耗电估算（推送运行）

| 项目 | 计算 | 耗电 |
|------|------|------|
| 白天活跃 (192次×15s) | ~48min × 60mA | 48 mAh |
| 白天睡眠 | ~912min × 0.03mA | 0.46 mAh |
| 夜间睡眠 + 7次快速唤醒 | 8h × 0.03mA | 0.24 mAh |
| **日均总计** | | **~49 mAh** |

### 日耗电估算（推送停止）

| 项目 | 计算 | 耗电 |
|------|------|------|
| 白天活跃 (192次×15s) | ~48min × 60mA | 48 mAh |
| 白天睡眠 | ~912min × 0.03mA | 0.46 mAh |
| 夜间睡眠 + 7次快速唤醒 | 8h × 0.03mA | 0.24 mAh |
| **日均总计** | | **~49 mAh** |

### 续航估算（以 1500mAh 电池为例）

| 场景 | 日均耗电 | 实际续航（留 20% 余量） |
|------|---------|----------------------|
| 推送运行 | ~49 mAh | **~24 天** |
| 推送停止 | ~49 mAh | **~24 天** |

> 唤醒窗口从 30s 减至 15s 后，推送运行与停止的耗电基本一致（每次都只活跃 ~15s）。

## 固件实现

固件源码位于 `jcalendar-1.1.9/src/`。

### 关键文件

- `main.cpp` — 启动决策、唤醒/睡眠控制、RTC 同步、夜间快速路径
- `app_config.h` — 所有省电相关参数
- `battery.cpp` — 电池电压读取（`analogReadMilliVolts` + eFuse 校准）

### 关键函数

| 函数 | 文件 | 说明 |
|------|------|------|
| `is_battery_powered()` | `main.cpp` | 检测电池/USB 供电 |
| `seconds_to_next_wake()` | `main.cpp` | 计算下次唤醒时间（含夜间分段睡眠上限） |
| `enter_deep_sleep()` | `main.cpp` | 进入深度睡眠 |
| `sync_rtc_from_dashboard()` | `main.cpp` | 从推送数据同步 RTC + 首次同步激活省电 |
| `mark_sleep_after_render()` | `main.cpp` | 渲染完成后标记睡眠 |
| `notify_dashboard_data()` | `main.cpp` | 推送到达时同步 RTC |

### 跨深睡眠状态（RTC_DATA_ATTR）

| 变量 | 说明 |
|------|------|
| `g_rtcTimeSyncMode` | 上次是否处于省电模式（夜间快速路径判断） |
| `g_rtcLastSyncTime` | 上次 RTC 同步时间戳（3 天超时判断） |

## 脚本实现

### 整点对齐 (`tools/token_dashboard_host/main.py`)

脚本计算下一个 5 分钟整点 +5 秒的时间，等待到该时刻再执行采集和推送。

### 时间编码 (`tools/token_dashboard_host/renderer/firmware_render.py`)

DashboardSnapshotV1 的末尾 3 字节编码当前时间，供设备同步时钟。

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

2. 固件串口日志（首次刷固件，RTC 未同步）：
   ```
   RTC not ready (valid=0, sync_age=0s), staying awake for time sync
   ```
   推送到达后：
   ```
   RTC synced from dashboard: 10:05
   RTC synced, time sync mode activated
   ```

3. 正常省电模式日志：
   ```
   Time sync mode enabled (battery power saving)
   ```

4. 夜间分段睡眠日志：
   ```
   Night mode: capped sleep 28800s -> 3600s
   Night mode: continuing sleep (fast path)
   ```

5. 屏幕正常刷新，数据正确显示

## 相关文件

### 固件 (`jcalendar-1.1.9/src/`)

- `main.cpp` — 启动决策、智能唤醒、RTC 同步、夜间睡眠
- `app_config.h` — 省电参数配置
- `battery.cpp` — 电池电压读取
- `protocol.cpp` — BLE 命令分发

### 脚本 (`tools/token_dashboard_host/`)

- `renderer/firmware_render.py` — 时间编码
- `main.py` — 整点对齐
