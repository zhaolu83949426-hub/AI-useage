# Token 用量看板详细设计文档

## 1. 设计目标

基于 `jcalendar-1.1.9` 固件（z21 环境）和 `tools/token_dashboard_host/` 主机端程序，实现 ESP32 + 4.2 寸 400×300 黑白红三色墨水屏的 Token 用量看板。

系统同时支持两种渲染模式：

1. **bitmap**：主机渲染 PIL 图像 → BLE 发送双平面位图 → 设备显示
2. **firmware_render**：主机发送 192B 结构化快照 → 设备本地模板渲染

两种模式共用同一套主机采集逻辑，由 `TOKEN_DASHBOARD_RENDER_MODE` 配置切换。

## 2. 范围

### 2.1 覆盖

1. Host 侧统一数据模型和采集逻辑
2. 双模式发送路径
3. BLE 命令协议（`0x0070-0x0072` 位图模式，`0x0078-0x007A` 结构化模式）
4. 设备侧协议状态机和渲染器
5. DashboardSnapshotV1 二进制格式

### 2.2 不覆盖

1. WiFi 传输
2. 局部刷新协议
3. 通用中文字库系统
4. 通用 UI 布局引擎

## 3. 总体架构

### 3.1 Host 侧组件

```
Scheduler (每 300s)
  → Collectors (aiusage / glm_plan / gpt_plan)
  → DashboardSnapshot
  → Transport Encoder
     ├─ bitmap: PIL 渲染 → RGB 位平面 → 0x0070/0x0071/0x0072
     └─ firmware_render: DashboardSnapshotV1 → 0x0078/0x0079/0x007A
```

### 3.2 Device 侧组件

```
BLE Command Dispatcher (protocol.cpp)
  ├─ Direct Write Runtime (位图模式)
  │   └─ 双平面接收 → render_bitplane_image()
  └─ Dashboard Protocol Runtime (结构化模式)
      └─ 快照接收 → CRC 校验 → render_dashboard()
```

### 3.3 数据流

**位图模式：**
```
Collectors → DashboardSnapshot → PIL Renderer → bitplanes → 0x0070/71/72 → 全刷
```

**结构化模式：**
```
Collectors → DashboardSnapshot → binary encoder → 0x0078/79/7A → 本地渲染 → 全刷
```

## 4. Host 侧设计

### 4.1 模式配置

`tools/token_dashboard_host/config.py`：

```python
TOKEN_DASHBOARD_RENDER_MODE = "bitmap"  # 或 "firmware_render"
```

规则：
- `bitmap`：走位图链路
- `firmware_render`：走结构化链路
- 未配置时默认 `bitmap`
- 不做自动探测或切换

### 4.2 统一快照模型

`tools/token_dashboard_host/renderer/snapshot.py` 定义 `DashboardSnapshot`，两种模式共用：

```json
{
  "generatedAt": "2026-05-25T09:24:00+08:00",
  "lastRefreshLabel": "09:24",
  "today": {
    "totalTokens": 71500000,
    "inputTokens": 24800000,
    "outputTokens": 39200000,
    "cacheTokens": 7500000
  },
  "plans": {
    "glm": { "planLevel": "PRO", "fiveHourPercent": 82, "weekPercent": 64, ... },
    "gpt": { "fiveHourPercent": 43, "weekPercent": 57, ... }
  },
  "models": [
    { "model": "glm-5.1", "provider": "zhipu", "calls": 48, "tokens": 620000, "sharePercent": 49.8 }
  ]
}
```

### 4.3 采集器

| 采集器 | 数据源 | 输出 |
|--------|--------|------|
| `collectors/aiusage.py` | AIUsage HTTP API (`localhost:3847`) | totalTokens, inputTokens, outputTokens, cacheTokens, topModels |
| `collectors/glm_plan.py` | 智谱开放平台 API | planLevel, fiveHourPercent, weekPercent, 时间标签 |
| `collectors/gpt_plan.py` | Codex 本地会话日志 `rate_limits` | fiveHourPercent, weekPercent, 时间标签 |

## 5. BLE 协议设计

### 5.1 保留命令（位图模式）

| 命令 | 作用 |
|------|------|
| `0x0070` | DIRECT_WRITE_START |
| `0x0071` | DIRECT_WRITE_DATA |
| `0x0072` | DIRECT_WRITE_END |

### 5.2 新增命令（结构化模式）

| 命令 | 名称 | 作用 |
|------|------|------|
| `0x0078` | DASHBOARD_RENDER_START | 开始接收结构化快照 |
| `0x0079` | DASHBOARD_RENDER_DATA | 接收快照数据分块 |
| `0x007A` | DASHBOARD_RENDER_COMMIT | 校验、渲染并刷新 |

### 5.3 响应

| 响应 | 含义 |
|------|------|
| `[0x00, 0x78]` | START ACK |
| `[0x00, 0x79]` | DATA ACK |
| `[0x00, 0x7A]` | COMMIT ACK |
| `[0x00, 0x7B]` | 渲染成功 |
| `[0x00, 0x7C]` | 渲染失败 |
| `[0xFF, opcode, error, 0x00]` | 协议错误 |

### 5.4 错误码

| 值 | 含义 |
|----|------|
| 0x01 | 版本不支持 |
| 0x02 | payload 长度非法 |
| 0x04 | CRC 校验失败 |
| 0x05 | 状态机错误 |
| 0x07 | 设备忙 |
| 0x08 | 刷新模式不支持 |

## 6. DashboardSnapshotV1 格式

固定 192 字节，little-endian：

```text
offset  size  field
0       1     schema_version
1       1     row_count
2       1     glm_5h_percent
3       1     glm_week_percent
4       1     gpt_5h_percent
5       1     gpt_week_percent
6       1     glm_level_len
7       1     reserved
8       4     total_tokens_u32
12      4     input_tokens_u32
16      4     output_tokens_u32
20      4     cache_tokens_u32
24      5     last_refresh_ascii
29      5     glm_5h_label_ascii
34      5     glm_week_label_ascii
39      5     gpt_5h_label_ascii
44      5     gpt_week_label_ascii
49      8     glm_level_ascii
57      132   model_rows[4]
189     3     reserved
```

模型行 (每行 33B)：

```text
offset  size  field
0       24    model_ascii
24      1     provider_code (1=zhipu, 2=openai, 3=anthropic, 4=google, 255=other)
25      2     calls_u16
27      4     total_tokens_u32
31      2     share_bp_u16 (百分比基点，100.00% = 10000)
```

## 7. Device 侧设计

### 7.1 模块

| 文件 | 职责 |
|------|------|
| `protocol.cpp/h` | BLE 命令分发，延迟任务调度 |
| `dashboard_protocol.cpp/h` | 快照协议状态机，缓冲区管理，CRC 校验 |
| `dashboard_renderer.cpp/h` | 固定模板渲染，颜色判定，字段格式输出 |
| `display_service.cpp/h` | 底层显示驱动 |

### 7.2 协议状态机

```
IDLE ──START──> RECEIVING ──收满──> READY_TO_COMMIT ──COMMIT──> RENDERING ──完成──> IDLE
```

- RECEIVING 状态收到 START：重置并按新请求开始
- RENDERING 状态收到任意命令：返回 busy

### 7.3 渲染延后机制

`protocol.cpp` 中 `process_deferred_job()` 在 BLE 响应队列清空后才执行渲染，避免传输与屏幕刷新互相阻塞。延迟固定 80ms。

### 7.4 固定模板渲染

设备端不做通用排版，只绘制固定区域：

1. 顶部概览框（总量/输入/输出/缓存）
2. 左右套餐卡片（GLM / GPT，各含 5 小时 + 一周进度条）
3. 模型表格（最多 4 行）
4. 底部刷新时间
5. 右上角电池电量

### 7.5 颜色规则

- 黑色：正文、边框、分隔线、普通进度条
- 红色：套餐百分比 ≥ 80% 的填充和文本

### 7.6 本地状态

firmware_render 模式下设备本地读取电池电压，不再需要 Host 发 `READ_MSD`。

## 8. 异常处理

### Host 侧

- 数据源失败 → 不发新 payload，保留旧画面
- 编码失败 → 终止本轮
- BLE 发送失败 → 终止本轮

### Device 侧

- CRC 失败 → 返回错误，不刷新
- 字段非法 → 返回错误，不刷新
- 渲染失败 → 返回 0x7C，保留旧画面

## 9. 验收标准

1. `bitmap` 模式继续可用
2. `firmware_render` 模式可独立启用
3. Host 可稳定发送 `DashboardSnapshotV1`（192B）
4. Device 可稳定解析并渲染模板
5. 结构化模式下 BLE 传输耗时可忽略
6. 屏幕字段完整、颜色规则正确、无错位
