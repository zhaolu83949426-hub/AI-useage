# Token 用量看板使用文档

## 项目概述

Token 用量看板是一个基于 ESP32 和 400×300 黑白红三色墨水屏的 AI 用量监控显示系统。主机端 Python 程序定时采集 AI 服务用量数据，通过 BLE 推送到 ESP32 设备，设备在墨水屏上渲染看板页面。

设备端固件基于 **J-Calendar** (jcalendar-1.1.9) `z21` 环境，内嵌看板渲染模块。

### 主要功能

- 今日 Token 总量、输入、输出、缓存统计
- GLM CodingPlan 和 GPT Plus 套餐用量（5 小时 / 一周窗口）
- Top 4 模型调用排行及占比
- 设备电池电量显示
- 5 分钟自动刷新 + 数据变更缓存机制

---

## 环境要求

### 硬件

- ESP32 开发板
- 4.2 寸 400×300 黑白红三色墨水屏
- 蓝牙支持的电脑（用于 Host 端 BLE 通信）

### 固件硬件配置

| 项目 | 值 |
|------|-----|
| 固件工程 | `jcalendar-1.1.9/` |
| 编译环境 | `z21` |
| 分辨率 | `400 × 300` |
| 分页高度 | `32` |
| `BUSY` | `GPIO4` |
| `CS` | `GPIO5` |
| `RST` | `GPIO16` |
| `DC` | `GPIO17` |
| `CLK` | `GPIO18` |
| `DIN` | `GPIO23` |
| 电池采样脚 | `GPIO32` |
| BLE Service UUID | `00002446-0000-1000-8000-00805F9B34FB` |

### 软件

- Python 3.8+
- PlatformIO（用于固件编译上传）
- Windows 操作系统（自启动脚本为 PowerShell）

---

## 安装步骤

### 1. 安装 Python 依赖

```bash
pip install pillow requests bleak
```

### 2. 配置 GLM API Token

创建 `tools/token_dashboard_host/.glm_token` 文件（用于获取 CodingPlan 数据）：

```
你的GLM_API_KEY
```

### 3. 编译并上传固件

```bash
cd jcalendar-1.1.9
pio run -e z21
```

烧录（推荐 esptool 直接烧录，避免编码问题）：

```bash
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

将 `COM4` 替换成你的实际串口。

---

## 配置说明

### 渲染模式

编辑 `tools/token_dashboard_host/config.py`：

```python
TOKEN_DASHBOARD_RENDER_MODE = "bitmap"  # 推荐：Host 端渲染，字体清晰
# TOKEN_DASHBOARD_RENDER_MODE = "firmware_render"  # 设备端渲染，传输快
```

两种模式：

- `bitmap`：Host 渲染完整图像，通过 `0x0070/0x0071/0x0072` 发送黑白红双平面位图
- `firmware_render`：Host 发送 192B `DashboardSnapshotV1`，设备通过 `0x0078/0x0079/0x007A` 在本地渲染

### 刷新间隔

```python
REFRESH_INTERVAL_SECONDS = 300  # 5 分钟
```

### 数据源

看板从以下来源获取数据：

- AIUsage API：`http://localhost:3847/api/summary?range=day` 和 `/api/models?range=day`
- GLM 套餐：`https://open.bigmodel.cn/api/monitor/usage/quota/limit`
- GPT 套餐：Codex 本地会话日志中的 `rate_limits` 数据

确保 AIUsage 服务已启动。

---

## 运行方式

### 手动启动

```bash
python -m tools.token_dashboard_host.main
```

### 设置开机自启动

```powershell
cd tools\token_dashboard_host
.\setup_autostart.ps1
```

该脚本会创建：
1. **启动文件夹快捷方式**（当前用户 Startup 目录）
2. **任务计划程序任务**（`TokenDashboardAutoStart`，用户登录时触发）

### 取消自启动

```powershell
.\remove_autostart.ps1
```

---

## 日志和调试

### 查看日志

运行时日志输出到控制台：

```
2026-05-26 12:00:15 [INFO] dashboard: Token 用量看板 started
2026-05-26 12:00:15 [INFO] dashboard: Collecting data...
2026-05-26 12:00:19 [INFO] dashboard: Connecting to device...
2026-05-26 12:00:22 [INFO] dashboard: Device: WiFi=disconnected, battery=100%
2026-05-26 12:00:22 [INFO] dashboard: Data changed, proceeding with update
2026-05-26 12:00:22 [INFO] dashboard: Update succeeded
```

### Debug 图片

`bitmap` 模式下每次更新会生成 `dashboard_debug.png`。

### 缓存文件

位置：`.dashboard_cache.json`（项目根目录）

```bash
# 清除缓存（强制下次更新）
del .dashboard_cache.json
```

---

## 故障排查

### 数据为空，只有套餐信息

旧进程仍在运行，使用了错误的数据源。

```powershell
Get-Process python | Stop-Process -Force
python -m tools.token_dashboard_host.main
```

### GLM CodingPlan 数据为空

检查 `tools/token_dashboard_host/.glm_token` 文件是否存在且内容正确。

### 设备连接失败

1. 检查 ESP32 是否连接到正确串口
2. 按 RST 按钮重启设备
3. 检查是否有其他 BLE 连接占用
4. 日志中 BLE 设备名应以 `OD` 开头

### 界面显示异常

固件与 Host 端渲染模式不匹配时：

```bash
# 重新编译上传固件
cd jcalendar-1.1.9
pio run -e z21 -t upload

# 确认 config.py 中 TOKEN_DASHBOARD_RENDER_MODE 与固件支持一致
```

---

## 文件结构

```
jcalendar-1.1.9/
├── platformio.ini           # PlatformIO 构建配置（z21/z15/z98/1680 环境）
├── README.md                # 固件功能介绍
└── src/
    ├── main.cpp             # 入口
    ├── app_config.h         # 引脚、BLE 常量
    ├── protocol.cpp/h       # BLE 命令分发
    ├── ble_service.cpp/h    # BLE 初始化和广播
    ├── display_service.cpp/h      # 显示驱动
    ├── dashboard_protocol.cpp/h   # 看板快照协议状态机
    └── dashboard_renderer.cpp/h   # 看板模板渲染

tools/token_dashboard_host/
├── main.py                  # 主入口
├── config.py                # 全局配置
├── collectors/
│   ├── aiusage.py           # AIUsage API 数据收集
│   ├── glm_plan.py          # GLM CodingPlan 数据收集
│   └── gpt_plan.py          # GPT Plus 套餐数据收集
├── renderer/
│   ├── dashboard.py         # PIL 渲染（bitmap 模式）
│   ├── bitmap.py            # RGB → 黑白红位平面转换
│   ├── firmware_render.py   # DashboardSnapshotV1 编码器
│   └── snapshot.py          # 数据快照结构
├── ble/
│   └── transport.py         # BLE 传输层
├── setup_autostart.ps1      # 自启动安装脚本
├── remove_autostart.ps1     # 自启动卸载脚本
└── .glm_token               # GLM API Token 配置
```

---

## 常用命令

```bash
# 启动看板
python -m tools.token_dashboard_host.main

# 停止看板
Get-Process python | Stop-Process -Force

# 清除缓存
del .dashboard_cache.json

# 测试 API
curl http://localhost:3847/api/summary?range=day
curl http://localhost:3847/api/models?range=day

# 编译固件
cd jcalendar-1.1.9
pio run -e z21

# 烧录固件 (esptool)
PYTHONIOENCODING=utf-8 "$HOME/.platformio/penv/Scripts/esptool.exe" /
  --chip esp32 --port COM4 --baud 115200 \
  --before default-reset --after hard-reset \
  write-flash -z --flash-mode dio --flash-freq 40m --flash-size detect \
  0x1000 .pio/build/z21/bootloader.bin \
  0x8000 .pio/build/z21/partitions.bin \
  0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 .pio/build/z21/firmware.bin \
  2>&1 | cat

# 查看调试图片 (bitmap 模式)
# 项目根目录下 dashboard_debug.png
```
