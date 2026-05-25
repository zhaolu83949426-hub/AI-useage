# Token 用量看板详细设计文档

## 1. 设计目标

基于当前仓库已具备的 `ESP32 + 4.2 寸 400x300 黑白红三色墨水屏` 固件能力，设计一套最直接、最易落地的方案，实现：

- 主机侧每 `5 分钟` 聚合 Token 数据；
- 主机侧生成完整看板图像；
- 通过 BLE 将图像推送至 ESP32；
- 设备侧刷新整页墨水屏；
- 页面内容严格遵守需求文档规定的字段与排版。

本设计只采用一种方案：**主机侧聚合数据并渲染整页看板位图，ESP32 侧仅负责接收、显示与提供本机状态数据**。

## 2. 现状确认

### 2.1 已确认可复用能力

1. 当前仓库已验证 `esp32-N4 + 4.2 寸 400x300 黑白红三色墨水屏` 可启动、显示和刷新。
2. 当前仓库 BLE 协议已存在 `DIRECT_WRITE_START / DIRECT_WRITE_DATA / DIRECT_WRITE_END` 图像写入链路，可作为看板图像下发通道。
3. 本机 `aiusage` 已安装，可通过 `aiusage status` 确认本地 SQLite 数据库路径为：
   - `C:\Users\zhaolu\.aiusage\cache.db`
4. `aiusage` 数据库中存在 `records`、`v_usage_records`、`v_sessions` 等视图，足以支撑：
   - 今日输入/输出/缓存/总量；
   - 按模型与提供商聚合；
   - 当日模型 TopN 排序。

### 2.2 已确认但不能直接用于完整实现的能力

1. `GLM CodingPlan` 已确认可通过智谱在线接口获取套餐数据：
   - `GET https://open.bigmodel.cn/api/monitor/usage/quota/limit`
   - `Authorization: Bearer <GLM_API_KEY>`
2. 该接口已返回真实数据，当前 `limits[]` 的界面映射规则已经确认。
3. 当前 Codex 本地会话日志已确认持续写入 `rate_limits`：
   - `primary.window_minutes = 300`
   - `secondary.window_minutes = 10080`
   - `plan_type = plus`
4. 因此 GPT Plus 的 `5 小时 / 一周` 套餐进度，已可直接从本机 Codex 会话日志提取，无需额外猜测远程 undocumented 接口。

## 3. 方案选型

### 3.1 选定方案

采用 **主机渲染整页位图 + BLE 全屏下发** 方案。

### 3.2 方案理由

1. 当前固件已经具备图像接收与墨水屏刷新能力，可最大化复用现有仓库能力。
2. 400x300 三色墨水屏的复杂表格、中文文本、细分隔线和双色进度条，在主机侧渲染更稳定。
3. ESP32 经典款内存有限，若在设备端做完整中文排版与表格布局，复杂度显著更高。
4. 主机侧更方便直接访问 `aiusage` 本地数据库与后续套餐接口。

### 3.3 明确不采用的方案

不采用“仅推送结构化数据，由 ESP32 本地排版整页”的方案。

原因：

- 需要在固件内新增完整布局引擎、中文字体管理、进度条绘制与多表格对齐逻辑；
- 对经典 ESP32 的内存和开发复杂度不友好；
- 与当前仓库“已能下发图像”的现状相比，改动面更大。

## 4. 总体架构

## 4.1 组件划分

系统拆分为五个组件：

1. `Host Scheduler`
   - 定时触发采集任务。
2. `Host Data Collector`
   - 从 `aiusage`、GLM 套餐源、GPT 套餐源采集数据。
3. `Host Renderer`
   - 将数据渲染为 `400x300` 三色看板位图。
4. `BLE Transport`
   - 将位图发送给设备。
5. `ESP32 Display Runtime`
   - 暴露本机状态；
   - 接收图像并刷新屏幕。

## 4.2 数据流

```text
定时器(5分钟)
  -> 读取 aiusage 今日数据
  -> 读取 GLM 套餐数据
  -> 读取 GPT 套餐数据
  -> 读取设备状态(电量、Wi-Fi)
  -> 组装 DashboardSnapshot
  -> 渲染 400x300 三色位图
  -> BLE 下发位图
  -> ESP32 刷新墨水屏
```

## 5. 模块设计

## 5.1 Host Scheduler

### 职责

- 每 `300` 秒触发一次看板刷新任务；
- 串行执行，不并发叠加；
- 仅在上一轮任务结束后开启下一轮。

### 触发规则

- 程序启动后立即执行一次；
- 之后固定每 `5 分钟` 执行一次。

### 失败规则

- 任意关键数据源失败，则本轮视为失败；
- 本轮失败时不推送新图；
- 墨水屏保留上一次成功画面；
- 错误只记录主机日志，不在屏幕追加额外状态文字。

## 5.2 Host Data Collector

### 5.2.1 输出模型

主机侧统一产出 `DashboardSnapshot`：

```json
{
  "generatedAt": "2026-05-25T09:24:00+08:00",
  "lastRefreshLabel": "09:24",
  "device": {
    "wifiConnected": true,
    "batteryPercent": 87
  },
  "today": {
    "totalTokens": 71500000,
    "inputTokens": 24800000,
    "outputTokens": 39200000,
    "cacheTokens": 7500000
  },
  "plans": {
    "glm": {
      "title": "GLM CodingPlan 套餐",
      "fiveHourPercent": 82,
      "fiveHourLabel": "22:17",
      "weekPercent": 64,
      "weekLabel": "5月31日"
    },
    "gpt": {
      "title": "GPT Plus 套餐",
      "fiveHourPercent": 43,
      "fiveHourLabel": "22:17",
      "weekPercent": 57,
      "weekLabel": "5月31日"
    }
  },
  "models": [
    {
      "model": "glm-4.7",
      "provider": "zhipu",
      "calls": 454,
      "tokens": 32000000,
      "sharePercent": 45.2
    }
  ]
}
```

### 5.2.2 aiusage 采集器

#### 数据源

- SQLite：`C:\Users\zhaolu\.aiusage\cache.db`

#### 统计口径

- 按主机本地时区的自然日统计；
- 数据表：`records`；
- 单条总量：
  - `input_tokens + output_tokens + cache_read_tokens + cache_write_tokens + thinking_tokens`

#### 今日概览 SQL

```sql
SELECT
  SUM(input_tokens) AS input_tokens,
  SUM(output_tokens) AS output_tokens,
  SUM(cache_read_tokens) AS cache_read_tokens,
  SUM(input_tokens + output_tokens + cache_read_tokens + cache_write_tokens + thinking_tokens) AS total_tokens
FROM records
WHERE date(ts / 1000, 'unixepoch', 'localtime') = date('now', 'localtime');
```

#### 模型表格 SQL

```sql
SELECT
  model,
  provider,
  COUNT(*) AS calls,
  SUM(input_tokens + output_tokens + cache_read_tokens + cache_write_tokens + thinking_tokens) AS total_tokens
FROM records
WHERE date(ts / 1000, 'unixepoch', 'localtime') = date('now', 'localtime')
GROUP BY model, provider
ORDER BY total_tokens DESC
LIMIT 5;
```

#### 占比计算

- `share_percent = model_total_tokens / today_total_tokens * 100`

#### 格式化规则

- `>= 1,000,000`：显示 `M`，保留 `1` 位小数；
- `>= 1,000` 且 `< 1,000,000`：显示 `K`，保留 `1` 位小数；
- `< 1,000`：显示整数。

### 5.2.3 GLM 套餐采集器

#### 设计目标

输出：

- `fiveHourPercent`
- `fiveHourLabel`
- `weekPercent`
- `weekLabel`

#### 数据源

- 接口：`GET https://open.bigmodel.cn/api/monitor/usage/quota/limit`
- Header：`Authorization: Bearer <GLM_API_KEY>`

#### 真实返回验证

已验证接口可返回 `code=200`，样例字段包括：

- `data.level`
- `data.limits[].type`
- `data.limits[].unit`
- `data.limits[].number`
- `data.limits[].percentage`
- `data.limits[].nextResetTime`
- `data.limits[].usage`
- `data.limits[].currentValue`
- `data.limits[].remaining`

#### 当前观察到的真实样例特征

- 存在多个 `limits[]` 项；
- 当前真实返回中同时出现 `TOKENS_LIMIT` 与 `TIME_LIMIT`；
- 观察到的重置时间示例包括：
  - `2026-05-27 10:01:44`
  - `2026-06-13 10:01:44`

#### 映射规则

当前映射规则固定如下：

1. `type = TOKENS_LIMIT` 且 `unit = 3`
   - 含义：`5 小时使用额度`
   - 映射到：
     - `fiveHourPercent`
     - `fiveHourLabel`
2. `type = TOKENS_LIMIT` 且 `unit = 6`
   - 含义：`每周使用额度`
   - 映射到：
     - `weekPercent`
     - `weekLabel`
3. `type = TIME_LIMIT` 且 `unit = 5`
   - 含义：`MCP 每月额度`
   - 本期不进入墨水屏看板展示

#### 当前真实样例解释

1. `TOKENS_LIMIT + unit=3 + number=5 + percentage=0`
   - 对应 `5 小时使用额度`
2. `TOKENS_LIMIT + unit=6 + number=1 + percentage=15 + nextResetTime=2026-05-27 10:01:44`
   - 对应 `每周使用额度`
3. `TIME_LIMIT + unit=5 + number=1 + percentage=2 + nextResetTime=2026-06-13 10:01:44`
   - 对应 `MCP 每月额度`
   - 当前看板忽略

#### 接口约束

GLM 采集器后续必须向上层返回统一结构：

```json
{
  "fiveHourPercent": 82,
  "fiveHourLabel": "22:17",
  "weekPercent": 64,
  "weekLabel": "5月31日"
}
```

#### 建议实现方式

1. 主机侧使用 HTTPS 每 5 分钟请求一次；
2. 从 `data.limits[]` 中按以下规则筛选：
   - `TOKENS_LIMIT + unit=3` -> `5 小时`
   - `TOKENS_LIMIT + unit=6` -> `一周`
3. 读取 `percentage` 与 `nextResetTime`；
4. 将 `nextResetTime` 转换为：
   - 当日时间格式：`HH:mm`
   - 跨日重置格式：`M月D日`
5. 若 `5 小时` 项缺少 `nextResetTime`，则按固定窗口展示百分比，并由实现阶段为时间位输出固定占位文本。

### 5.2.4 GPT 套餐采集器

#### 设计目标

输出：

- `fiveHourPercent`
- `fiveHourLabel`
- `weekPercent`
- `weekLabel`

#### 数据源

- 本机 Codex 会话日志目录：
  - `C:\Users\zhaolu\.codex\sessions\`

#### 真实可读字段

当前最新会话文件中已存在如下真实记录：

```json
{
  "rate_limits": {
    "primary": {
      "used_percent": 19.0,
      "window_minutes": 300,
      "resets_at": 1779689258
    },
    "secondary": {
      "used_percent": 20.0,
      "window_minutes": 10080,
      "resets_at": 1780219073
    },
    "plan_type": "plus"
  }
}
```

对应本地时间：

- `primary.resets_at = 2026-05-25 14:07:38`
- `secondary.resets_at = 2026-05-31 17:17:53`

#### 设计结论

GPT Plus 套餐采集器直接读取本机最新 Codex 会话中的 `rate_limits` 即可：

- `primary` -> `5 小时`
- `secondary` -> `一周`

这样可以稳定获得：

- 使用百分比；
- 重置时间；
- 订阅类型。

#### 建议实现方式

1. 主机侧扫描 `C:\Users\zhaolu\.codex\sessions\` 下最新的 `rollout-*.jsonl`；
2. 逆序读取最近一条包含 `rate_limits` 的 `token_count` 事件；
3. 提取：
   - `primary.used_percent`
   - `primary.resets_at`
   - `secondary.used_percent`
   - `secondary.resets_at`
4. 将 `resets_at` 的 Unix 秒时间戳转换为本地时间文本。

## 5.3 Device Runtime Status 采集

### 5.3.1 设计目标

顶部右侧的 `Wi-Fi 已连接` 与 `87%` 不从主机硬编码，而是来自设备当前状态。

### 5.3.2 数据项

- `wifiConnected`
- `batteryPercent`

### 5.3.3 获取方式

主机在每次渲染前，先通过 BLE 向设备读取运行态信息，再将结果绘制到位图中。

### 5.3.4 固件侧新增需求

在现有 BLE 命令协议上新增一个轻量运行态读取命令：

- 命令名：`READ_RUNTIME_STATUS`
- 作用：返回设备当前 Wi-Fi 连接状态和电量百分比

建议响应结构：

```text
[status=0x00] [cmd_low] [wifi_connected:1byte] [battery_percent:1byte]
```

字段定义：

- `wifi_connected`
  - `0x00`：未连接
  - `0x01`：已连接
- `battery_percent`
  - `0-100`

### 5.3.5 文案规则

- `wifi_connected = 1` 时显示 `Wi-Fi 已连接`
- 本期需求未定义断网状态替代文案，因此断网状态在进入实现前需再确认是否允许显示 `Wi-Fi 未连接`

## 5.4 Host Renderer

### 5.4.1 渲染策略

主机侧生成整页 `400x300` 位图，并按三色电子纸要求输出：

- 白底；
- 黑色图层；
- 红色告警图层。

### 5.4.2 字体策略

建议使用清晰、偏粗的无衬线中文字体，统一在主机渲染。

推荐字号层级：

- 主标题：`22px`
- 顶栏状态：`11px`
- 今日总量值：`36px`
- 今日总量单位：`10px`
- 概览副值：`28px`
- 套餐标题：`16px`
- 套餐行标签：`12px`
- 套餐百分比：`18px`
- 表头：`12px`
- 表体：`11px`
- 底部刷新时间：`11px`

### 5.4.3 画布坐标

采用固定像素布局。

#### 总画布

- 宽：`400`
- 高：`300`
- 背景：白色

#### 区域坐标

| 区域 | X | Y | W | H |
|---|---:|---:|---:|---:|
| 顶部标题栏 | 8 | 6 | 384 | 24 |
| 标题栏下分隔线 | 8 | 30 | 384 | 1 |
| 今日概览区 | 8 | 36 | 384 | 60 |
| 套餐区左卡片 | 8 | 102 | 188 | 70 |
| 套餐区右卡片 | 204 | 102 | 188 | 70 |
| 模型表格区 | 8 | 178 | 384 | 96 |
| 底部分隔线 | 8 | 280 | 384 | 1 |
| 底部刷新时间区 | 8 | 286 | 160 | 10 |

### 5.4.4 顶栏布局

- 左侧标题起点：`(12, 11)`
- 右侧状态区从右向左排布：
  - 电量百分比
  - 电量图标
  - `Wi-Fi 已连接`

不显示时间。

### 5.4.5 今日概览区布局

四等分，每列宽度 `96px`。

- 第 1 列：今日总量
- 第 2 列：输入
- 第 3 列：输出
- 第 4 列：缓存

列间使用竖向虚线。

### 5.4.6 套餐卡片布局

每张卡片包含：

- 标题行；
- 两条用量行；
- 中间一条虚线分隔。

每条用量行固定对齐：

| 元素 | 左侧偏移 |
|---|---:|
| 时间范围标题 | 10 |
| 进度条起点 | 58 |
| 百分比起点 | 138 |
| 时间标签起点 | 168 |

进度条尺寸：

- 宽：`68px`
- 高：`10px`
- 边框：黑色 `1px`
- 填充：黑色或红色

### 5.4.7 表格布局

表格宽度 `384px`，列宽固定：

| 列 | 宽度 |
|---|---:|
| 模型 | 108 |
| 提供商 | 60 |
| 调用 | 44 |
| TOKEN | 68 |
| 占比 | 104 |

占比列内部结构：

- 进度条宽：`56px`
- 百分比文本宽：`40px`
- 进度条始终黑色，不使用红色。

### 5.4.8 底部区域布局

- 左对齐显示：`上次刷新：HH:mm`
- 不显示其他信息。

## 5.5 颜色与规则实现

### 5.5.1 黑色内容

- 所有正文；
- 所有边框；
- 所有分隔线；
- 普通套餐进度条；
- 模型占比进度条。

### 5.5.2 红色内容

只允许出现于套餐区满足 `percent >= 80` 的以下元素：

- 当前项进度条填充；
- 当前项百分比文本。

### 5.5.3 禁止项

渲染器不得输出：

- 灰度；
- 渐变；
- 阴影；
- 彩色背景；
- 额外装饰图标。

## 6. BLE 传输设计

### 6.1 传输方案

复用现有 BLE 图像写入链路：

1. `DIRECT_WRITE_START`
2. `DIRECT_WRITE_DATA`
3. `DIRECT_WRITE_END`

### 6.2 传输步骤

```text
1. 主机连接设备 BLE 服务
2. 读取 READ_RUNTIME_STATUS
3. 主机构造 DashboardSnapshot
4. 主机渲染完整位图
5. 主机发送 DIRECT_WRITE_START
6. 主机分块发送 DIRECT_WRITE_DATA
7. 主机发送 DIRECT_WRITE_END
8. 设备执行一次整屏刷新
```

### 6.3 分块要求

- 单块大小遵循当前固件已支持的 BLE 写入上限；
- 必须等待设备可接收后继续发送下一块；
- 任一块发送失败，则本轮终止。

## 7. 固件侧改动设计

### 7.1 改动范围

固件侧只做两类改动：

1. 新增 `READ_RUNTIME_STATUS` 命令；
2. 保持现有图像写入链路可稳定接收整页看板图像。

### 7.2 不做的改动

- 不在 ESP32 上实现整页看板排版；
- 不在设备端解析 `aiusage` 数据；
- 不在设备端实现套餐计算逻辑。

### 7.3 电量百分比来源

建议复用现有电池电压读取能力，按统一阈值换算百分比：

- `>= 4.20V` -> `100%`
- `<= 3.30V` -> `0%`
- 中间线性插值。

### 7.4 Wi-Fi 状态来源

复用现有 Wi-Fi 运行态：

- 已连接 AP 且已获取 IP：`wifiConnected = true`
- 否则：`false`

## 8. 主机侧实现建议

## 8.1 技术选型

主机侧建议单独实现为一个轻量 Python 程序。

原因：

- 可直接使用标准库 `sqlite3` 读取 `aiusage`；
- 可使用 `Pillow` 渲染位图；
- 可使用 `bleak` 在 Windows 上完成 BLE 通信；
- 不依赖固件仓库现有 C++ 工程链。

### 8.2 建议目录

后续实现建议新增：

```text
tools/
  token_dashboard_host/
    main.py
    collectors/
    renderer/
    ble/
```

## 9. 数据格式化规则

### 9.1 Token 数值格式

- `71,500,000 -> 71.5M`
- `55,400 -> 55.4K`
- `999 -> 999`

### 9.2 百分比格式

- 向界面输出整数或一位小数；
- 推荐套餐百分比显示整数；
- 模型占比显示一位小数。

### 9.3 时间格式

- 5 小时重置：`HH:mm`
- 一周重置：`M月D日`
- 底部刷新时间：`HH:mm`

## 10. 异常处理规则

### 10.1 aiusage 无数据

- 今日数据为 `0`；
- 模型表格为空；
- 本轮仍可渲染。

### 10.2 套餐数据缺失

- 由于需求明确要求 GLM 与 GPT 都必须展示两项；
- 因此套餐数据缺失时，本轮不生成新看板，不推送屏幕。

### 10.3 BLE 下发失败

- 立即终止当前轮次；
- 不生成半屏画面；
- 保留上一次成功画面。

## 11. 测试设计

### 11.1 数据层验证

- 验证 `aiusage` 今日聚合 SQL 输出；
- 验证模型 Top5 排序；
- 验证 Token 格式化规则。

### 11.2 渲染层验证

- 验证 `400x300` 画布尺寸；
- 验证黑白红三色限制；
- 验证 `>= 80%` 红色告警规则；
- 验证套餐两行严格对齐；
- 验证表格列宽与行分隔。

### 11.3 传输层验证

- 验证 BLE 连接；
- 验证运行态读取；
- 验证整页位图分块发送；
- 验证设备端完整刷新。

## 12. 实现前阻塞项

当前设计可以直接指导以下部分进入实现：

- 看板像素布局；
- `aiusage` 今日数据采集；
- GLM 在线套餐采集；
- GPT 本地 Codex 会话套餐采集；
- 主机渲染器；
- BLE 整页推图；
- ESP32 运行态读取命令。

当前剩余的主要不确定项只有一项：

1. 设备 Wi-Fi 断开时的顶部文案是否允许显示 `Wi-Fi 未连接`。
