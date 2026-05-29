# Token 用量看板 - 实现总结

## 已完成的工作

### Host 侧（Python）

1. **统一 DashboardSnapshot 模型** (`tools/token_dashboard_host/renderer/snapshot.py`)
   - `ModelUsage`、`PlanStatus`、`GLMPlanStatus`、`GPTPlanStatus`、`DeviceStatus` 等数据类
   - 两种渲染模式共用同一数据源

2. **Firmware Render Encoder** (`tools/token_dashboard_host/renderer/firmware_render.py`)
   - `DashboardSnapshotV1` 二进制编码器，固定 192B payload
   - CRC32 校验和计算
   - 模型名 ASCII 化处理

3. **双模式支持**
   - `config.py`: `TOKEN_DASHBOARD_RENDER_MODE` 配置（bitmap / firmware_render）
   - `ble/transport.py`: bitmap 模式用 `0x0070/0x0071/0x0072`，firmware_render 模式用 `0x0078/0x0079/0x007A`
   - `main.py`: 根据配置选择发送路径

### Device 侧（C++，集成在 jcalendar-1.1.9 中）

4. **BLE 协议状态机** (`src/dashboard_protocol.cpp/h`)
   - 状态：IDLE → RECEIVING → READY_TO_COMMIT → RENDERING
   - 处理 `0x0078` START、`0x0079` DATA、`0x007A` COMMIT
   - CRC32 校验、错误码处理

5. **看板渲染器** (`src/dashboard_renderer.cpp/h`)
   - `DashboardDataV1` 结构体定义
   - 二进制 payload 解析 `dashboard_parse_v1`
   - 固定模板渲染（Token 概览、套餐进度、模型表格）

6. **命令分发集成** (`src/protocol.cpp/h`)
   - 所有 BLE 命令统一在 `process_command_packet()` 分发
   - 渲染延后到响应队列清空后执行，避免 BLE 传输与屏幕刷新互相阻塞
   - COMMIT 成功后自动触发渲染

## 使用方法

### Host 侧

```bash
# bitmap 模式（默认）
python -m tools.token_dashboard_host.main

# firmware_render 模式
# 编辑 config.py: TOKEN_DASHBOARD_RENDER_MODE = "firmware_render"
python -m tools.token_dashboard_host.main
```

### Device 侧

固件已集成在 jcalendar-1.1.9 的 `z21` 环境中：

```bash
cd jcalendar-1.1.9
pio run -e z21 -t upload
```

## BLE 协议

### 位图模式（bitmap）

| 命令 | 说明 |
|------|------|
| `0x0070` | 直接写入开始，先接收黑平面 |
| `0x0071` | 数据传输，每块 ACK |
| `0x0072` | 平面收满自动切到红平面，全部收满触发刷新 |

### 结构化模式（firmware_render）

| 命令 | 说明 |
|------|------|
| `0x0078` | START：version + flags + payload_len + CRC32 |
| `0x0079` | DATA：payload 分块传输 |
| `0x007A` | COMMIT：触发解析、渲染和刷新 |

响应：
- `[0x00, 0x7B]` 渲染成功
- `[0x00, 0x7C]` 渲染失败
- `[0xFF, opcode, error, 0x00]` 协议错误

## 文件清单

### Host 端

- `tools/token_dashboard_host/main.py` — 主入口
- `tools/token_dashboard_host/config.py` — 渲染模式等配置
- `tools/token_dashboard_host/collectors/aiusage.py` — AIUsage API
- `tools/token_dashboard_host/collectors/glm_plan.py` — GLM 套餐
- `tools/token_dashboard_host/collectors/gpt_plan.py` — GPT 套餐
- `tools/token_dashboard_host/renderer/dashboard.py` — PIL 渲染
- `tools/token_dashboard_host/renderer/bitmap.py` — 位平面转换
- `tools/token_dashboard_host/renderer/firmware_render.py` — 结构化编码
- `tools/token_dashboard_host/renderer/snapshot.py` — 数据快照
- `tools/token_dashboard_host/ble/transport.py` — BLE 传输

### Device 端（jcalendar-1.1.9/src/）

- `protocol.cpp/h` — BLE 命令分发
- `dashboard_protocol.cpp/h` — 看板协议状态机
- `dashboard_renderer.cpp/h` — 看板模板渲染
- `display_service.cpp/h` — 显示驱动
- `app_config.h` — 引脚和常量定义

## 注意事项

1. **两种模式共用同一套采集逻辑**，区别只在发送阶段
2. **firmware_render 模式传输 192B**，bitmap 模式传输约 30KB，前者 BLE 传输快约 150 倍
3. **bitmap 模式完全保留**，不受 firmware_render 模式影响
4. **当前固件以 z21 环境为准**，硬件配置为 4/5/16/17/18/23 引脚 + 400×300 三色屏
