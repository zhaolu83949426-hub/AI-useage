# Token 用量看板详细设计文档

## 1. 设计目标

基于当前仓库已经可用的 `ESP32 + 4.2 寸 400x300 黑白红三色墨水屏 + BLE` 能力，设计一套可直接进入实现的详细方案，实现以下目标：

- 保留现有 `主机渲染位图 + BLE 推图` 模式；
- 新增 `主机采集结构化数据 + BLE 发送快照 + 固件固定模板渲染` 模式；
- 两种模式共用同一套主机采集逻辑；
- 在不改变蓝牙传输通道的前提下，将结构化模式的传输耗时压缩到接近 `0s`；
- 页面字段、颜色规则、刷新规则保持一致。

本设计只采用一种总体方案：**双模式并存，位图模式保留，固件端渲染模式新增，不做自动切换，不做隐式 fallback。**

## 2. 范围

### 2.1 本次设计覆盖

1. Host 侧统一数据模型
2. 双模式发送路径
3. 新 BLE 命令协议
4. 设备侧结构化快照解析
5. 设备侧固定模板看板渲染
6. 数据格式、错误码、校验规则
7. 验收与测试口径

### 2.2 本次设计不覆盖

1. WiFi 传输方案
2. 局部刷新协议落地
3. 通用中文字库系统
4. 通用 UI 布局引擎
5. 自动从位图模式降级或升级到固件渲染模式

## 3. 现状确认

### 3.1 已确认可复用能力

1. 当前仓库已验证 `esp32-N4 + 4.2 寸 400x300 黑白红三色墨水屏` 可启动、可整屏刷新。
2. 当前主机端已经具备：
   - `aiusage` 今日聚合
   - GLM 套餐采集
   - GPT 套餐采集
   - PIL 渲染
   - 位图双平面转换
   - BLE 下发图像
3. 当前固件端已经具备：
   - BLE 命令分发入口
   - `DIRECT_WRITE_START / DATA / END`
   - 双平面图像写入
   - Boot Screen 文本与像素绘制基础
4. 当前协议中 `0x0070-0x0076` 已被图像直写、局部写和设备控制占用。

### 3.2 当前性能结论

当前慢点已经明确在传输链路，不在主机采集：

- 双平面整图约 `30KB`
- BLE 逐 chunk ACK
- 三色屏整屏刷新固定 `12-15s`

因此本次详细设计的核心不是改采集逻辑，而是减少蓝牙传输字节数，并把模板渲染移动到设备侧。

## 4. 方案选型

### 4.1 选定方案

系统同时支持两种显式模式：

1. `bitmap`
   - 主机渲染 PIL 图像
   - 转双平面
   - 通过 `0x0070/0x0071/0x0072` 下发
2. `firmware_render`
   - 主机仅采集和组装快照
   - Host 将快照编码为固定长度二进制 payload
   - 通过新增命令下发
   - 设备使用固定模板渲染并刷新

### 4.2 为什么保留位图模式

1. 现有链路已经可用，是最稳定的回归基线。
2. 新模式第一阶段只作为新增能力，不应破坏现有链路。
3. 后续验证中如发现固件模板效果不满足要求，只需要切换配置，不需要回滚代码结构。

### 4.3 为什么新增固件端渲染

1. 当前页面是固定模板，不需要通用布局引擎。
2. 传输 30KB 位图远大于传输业务数据本身。
3. 动态内容几乎都是数字、ASCII 模型名和时间，适合设备侧固定模板绘制。
4. 当前固件已经具备基础文本像素绘制能力，不需要从零做图形管线。

## 5. 总体架构

## 5.1 Host 侧组件

1. `Scheduler`
   - 每 `300s` 触发一次刷新
2. `Collectors`
   - `aiusage`
   - `glm_plan`
   - `gpt_plan`
3. `Snapshot Builder`
   - 统一组装 `DashboardSnapshot`
4. `Transport Encoder`
   - `bitmap encoder`
   - `firmware_render encoder`
5. `BLE Transport`
   - 按模式发送不同命令

## 5.2 Device 侧组件

1. `BLE Command Dispatcher`
2. `Direct Write Runtime`
   - 现有位图模式继续复用
3. `Dashboard Protocol Runtime`
   - 新增结构化快照接收状态机
4. `Dashboard Snapshot Parser`
5. `Dashboard Renderer`
   - 固定模板绘制
6. `Display Refresh Runtime`
   - 统一调用底层刷新

## 5.3 数据流

### 位图模式

```text
Scheduler
  -> Collectors
  -> DashboardSnapshot
  -> PIL Renderer
  -> RGB to bitplanes
  -> DIRECT_WRITE_START/DATA/END
  -> Device full refresh
```

### 固件渲染模式

```text
Scheduler
  -> Collectors
  -> DashboardSnapshot
  -> Binary Snapshot Encoder
  -> DASHBOARD_RENDER_START/DATA/COMMIT
  -> Device parse snapshot
  -> Device fixed-template render
  -> Device full refresh
```

## 6. Host 侧设计

## 6.1 模式配置

Host 侧新增一个显式配置项：

```text
TOKEN_DASHBOARD_RENDER_MODE=bitmap|firmware_render
```

规则：

1. `bitmap`
   - 走现有链路
2. `firmware_render`
   - 走新增结构化链路
3. 未配置时默认 `bitmap`
4. 不做自动探测，不做自动切换

## 6.2 Scheduler

### 职责

- 程序启动立即执行一次
- 后续每 `300` 秒执行一次
- 串行执行，不允许重叠

### 执行顺序

1. 采集业务数据
2. 组装 `DashboardSnapshot`
3. 根据模式选择发送路径
4. 发送成功后记录本轮成功时间

### 失败规则

- 任意关键数据源失败，本轮不推送新画面
- BLE 发送失败，本轮终止
- 设备保留上一次成功画面

## 6.3 统一快照模型

Host 侧统一产出 `DashboardSnapshot`，两种模式共用：

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
    "glm": {
      "planLevel": "PRO",
      "fiveHourPercent": 82,
      "fiveHourLabel": "22:17",
      "weekPercent": 64,
      "weekLabel": "05-31"
    },
    "gpt": {
      "fiveHourPercent": 43,
      "fiveHourLabel": "14:07",
      "weekPercent": 57,
      "weekLabel": "05-31"
    }
  },
  "models": [
    {
      "model": "claude-4-sonnet",
      "provider": "anthropic",
      "calls": 48,
      "tokens": 620000,
      "sharePercent": 49.8
    }
  ]
}
```

说明：

1. 位图模式仍然可以继续附带设备状态后再渲染。
2. 固件渲染模式下，设备状态由设备本地读取，不再进入 Host payload。

## 6.4 采集器设计

### 6.4.1 aiusage 采集器

继续复用当前逻辑：

- 数据源：`C:\Users\zhaolu\.aiusage\cache.db`
- 口径：本地自然日
- 输出：
  - `totalTokens`
  - `inputTokens`
  - `outputTokens`
  - `cacheTokens`
  - `topModels`

### 6.4.2 GLM 套餐采集器

继续复用当前接口采集逻辑，输出：

- `planLevel`
- `fiveHourPercent`
- `fiveHourLabel`
- `weekPercent`
- `weekLabel`

### 6.4.3 GPT 套餐采集器

继续复用当前 Codex 本地会话日志解析逻辑，输出：

- `fiveHourPercent`
- `fiveHourLabel`
- `weekPercent`
- `weekLabel`

## 6.5 模式内发送逻辑

### 6.5.1 bitmap 模式

沿用当前实现：

1. 渲染整页图像
2. 转黑/红双平面
3. 发送 `0x0070`
4. 分块发送 `0x0071`
5. 发送 `0x0072`
6. 等待刷新完成响应

### 6.5.2 firmware_render 模式

执行顺序固定为：

1. 构建 `DashboardSnapshot`
2. 编码为 `DashboardSnapshotV1`
3. 计算 `CRC32`
4. 发送 `0x0078`
5. 分块发送 `0x0079`
6. 发送 `0x007A`
7. 等待刷新完成响应

## 6.6 无变化跳过

### bitmap 模式

建议比较业务快照摘要，不直接比较整张图。

### firmware_render 模式

直接比较 `DashboardSnapshotV1` 二进制内容：

- 完全一致则跳过本轮发送
- 不一致才发 BLE

注意：

- `lastRefreshLabel` 只能在发送成功后更新
- 否则会导致每轮 payload 都不同

## 7. BLE 协议设计

## 7.1 现有命令保留

下面这些命令不做任何改动：

| 命令 | 作用 |
|------|------|
| `0x0044` | `READ_MSD` |
| `0x0070` | `DIRECT_WRITE_START` |
| `0x0071` | `DIRECT_WRITE_DATA` |
| `0x0072` | `DIRECT_WRITE_END` |
| `0x0076` | `PARTIAL_WRITE_START` |

## 7.2 新增命令

建议新增独立命令组：

| 命令 | 名称 | 作用 |
|------|------|------|
| `0x0078` | `DASHBOARD_RENDER_START` | 开始接收结构化看板快照 |
| `0x0079` | `DASHBOARD_RENDER_DATA` | 继续接收快照数据 |
| `0x007A` | `DASHBOARD_RENDER_COMMIT` | 校验、渲染并触发刷新 |

## 7.3 响应定义

| 响应 | 含义 |
|------|------|
| `[0x00, 0x78]` | START ACK |
| `[0x00, 0x79]` | DATA ACK |
| `[0x00, 0x7A]` | COMMIT ACK |
| `[0x00, 0x7B]` | 刷新成功 |
| `[0x00, 0x7C]` | 刷新超时 |
| `[0xFF, opcode, errorCode, 0x00]` | 协议错误 |

## 7.4 错误码

| errorCode | 含义 |
|-----------|------|
| `0x01` | 版本不支持 |
| `0x02` | payload 长度非法 |
| `0x03` | payload 超过缓冲区上限 |
| `0x04` | CRC 校验失败 |
| `0x05` | 状态机错误 |
| `0x06` | 字段值非法 |
| `0x07` | 当前正在渲染，设备 busy |
| `0x08` | 刷新模式不支持 |

## 7.5 START 帧

请求格式：

```text
[0x00, 0x78]
[version:1]
[flags:1]
[payload_len_le:2]
[crc32_le:4]
[optional_initial_payload...]
```

字段说明：

- `version`
  - 固定 `1`
- `flags`
  - bit0: 请求 `FULL` 刷新
  - bit1: 请求 `FAST` 刷新
  - bit2-bit7: 保留
- `payload_len_le`
  - `DashboardSnapshotV1` 总长度
- `crc32_le`
  - Host 对完整 payload 计算的 CRC32

规则：

1. START 到达时清理上一次 dashboard render 状态。
2. 如果可选初始 payload 已经带了一部分数据，也要计入接收长度。

## 7.6 DATA 帧

请求格式：

```text
[0x00, 0x79]
[payload_chunk...]
```

规则：

1. 每个 chunk 收到后立即 ACK
2. 累积长度不能超过 START 声明的 `payload_len`
3. 累积长度不能超过固件缓冲区上限

## 7.7 COMMIT 帧

请求格式：

```text
[0x00, 0x7A]
[refresh_mode:1]
```

字段定义：

- `0`
  - `FULL`
- `1`
  - `FAST`

规则：

1. COMMIT 前必须先收满 `payload_len`
2. 先做 CRC32 校验
3. 再做字段合法性校验
4. 校验通过后再进入绘制和刷新

## 8. DashboardSnapshotV1 设计

## 8.1 编码约束

- 字节序：`little-endian`
- 动态字符串编码：`ASCII`
- 动态模型条数：最大 `4`
- 固定中文标签：设备内置，不由 Host 发送

## 8.2 总体布局

固定长度 `192B`：

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

## 8.3 字段解释

### 百分比字段

- 范围：`0-100`
- 非法值：`>100`

### Token 数量字段

- 类型：`uint32`
- 单位：原始 token 数量

### 时间标签字段

格式约束：

- `5 小时`：`HH:mm`
- `一周`：`MM-DD`
- `last_refresh`：`HH:mm`

长度固定 `5` 字节，不足补 `0`。

### GLM 套餐等级

- 最长 `8` 字节
- 只允许 ASCII
- 示例：
  - `FREE`
  - `PLUS`
  - `PRO`

## 8.4 模型行结构

每行固定 `33B`：

```text
offset  size  field
0       24    model_ascii
24      1     provider_code
25      2     calls_u16
27      4     total_tokens_u32
31      2     share_bp_u16
```

## 8.5 provider_code 映射

| 值 | provider |
|----|----------|
| `1` | `zhipu` |
| `2` | `openai` |
| `3` | `anthropic` |
| `4` | `google` |
| `255` | `other` |

## 8.6 share_bp_u16

- 单位：百分比基点
- `100.00% = 10000`
- `49.8% = 4980`

## 8.7 Host 侧模型名规则

Host 在编码前必须保证：

1. 模型名仅保留 ASCII
2. 超过 `24` 字节时裁剪
3. 非 ASCII 字符替换为约定别名或 `_`

## 9. Device 侧设计

## 9.1 模块划分

建议新增下面 3 个文件：

```text
src/dashboard_protocol.h
src/dashboard_protocol.cpp
src/dashboard_renderer.h
src/dashboard_renderer.cpp
```

职责：

1. `dashboard_protocol`
   - BLE 结构化快照状态机
   - 缓冲区管理
   - CRC 校验
   - 字段解析
2. `dashboard_renderer`
   - 固定模板绘制
   - 颜色判定
   - 字段格式输出

## 9.2 协议状态机

状态建议如下：

1. `IDLE`
2. `RECEIVING`
3. `READY_TO_COMMIT`
4. `RENDERING`

状态迁移：

```text
IDLE --START--> RECEIVING
RECEIVING --all bytes received--> READY_TO_COMMIT
READY_TO_COMMIT --COMMIT--> RENDERING
RENDERING --success/fail--> IDLE
```

约束：

1. `RECEIVING` 状态再次收到 START，直接重置并按新请求开始。
2. `RENDERING` 状态收到任意 dashboard render 命令，返回 `busy`。

## 9.3 缓冲区设计

建议设备端单独维护一块 dashboard payload 缓冲区：

- 大小固定 `256B`
- 仅供 `DashboardSnapshotV1` 使用

原因：

- 当前 payload 固定 `192B`
- 留出协议头和后续小幅扩展空间
- 不和图像压缩缓冲复用，避免状态混乱

## 9.4 本地状态读取

固件渲染模式中，设备本地读取：

- `wifiConnected`
- `batteryPercent`

规则：

1. `wifiConnected`
   - 已连接 AP 且已获取 IP 时为真
2. `batteryPercent`
   - 继续复用现有电压转百分比规则

这样 Host 不再需要在固件渲染模式下额外发起 `READ_MSD`。

## 9.5 固定模板渲染

设备侧不做通用布局，只做固定模板。

### 模板区域

1. 顶部概览框
2. 左套餐卡片
3. 右套餐卡片
4. 模型表格
5. 底部刷新时间
6. 右上角设备状态

### 固定标签

下面这些中文标签内置到设备端，不从 Host 传：

- `今日总量TOKEN`
- `输入`
- `输出`
- `缓存`
- `5小时`
- `一周`
- `上次刷新`
- `GLM CodingPlan`
- `GPT Plus`
- `占比`
- `调用`

### 动态字段

设备端仅绘制下面这些动态内容：

- 数字型 token 数值
- 百分比
- 时间标签
- GLM 套餐等级
- ASCII 模型名
- provider 缩写

## 9.6 绘图原语

建议第一版只实现下面这些原语：

1. 画实线矩形边框
2. 画实线分隔线
3. 画简单虚线
4. 画纯色填充条
5. 画 ASCII 文本
6. 画固定中文标签位图

第一版不追求：

- 圆角完全一致
- 所有虚线节奏与 PIL 完全一致
- 字号像素级复刻

## 9.7 颜色规则

保持与现有位图模式一致：

### 黑色

- 正文
- 边框
- 分隔线
- 普通进度条
- 表格占比条

### 红色

仅当套餐百分比 `>= 80` 时，对应：

- 进度条填充
- 百分比文本

## 9.8 刷新规则

### 第一版要求

1. 默认只允许 `FULL`
2. 协议中可以预留 `FAST`
3. 若收到 `FAST` 且当前设备不允许，返回 `0x08`

原因：

- 当前目标先保证三色屏观感稳定
- 不把第一版复杂度扩展到刷新策略试验

## 10. 页面布局规则

设备端布局目标与当前页面保持一致，但允许实现细节有小幅偏差。

### 10.1 画布

- 宽：`400`
- 高：`300`
- 背景：白色

### 10.2 概览区

- 4 列固定布局
- 显示：
  - 今日总量
  - 输入
  - 输出
  - 缓存

### 10.3 套餐区

- 左：GLM
- 右：GPT
- 每卡片 2 行：
  - `5小时`
  - `一周`

### 10.4 表格区

- 最多 `4` 行模型
- 列内容：
  - 模型
  - 调用
  - TOKEN
  - 占比

### 10.5 底部

- `上次刷新：HH:mm`

## 11. Host 与 Device 的职责边界

## 11.1 Host 负责

1. 业务数据采集
2. 业务数据聚合
3. 时间标签生成
4. 模型名 ASCII 化
5. 二进制 payload 编码
6. CRC32 计算
7. BLE 分块发送

## 11.2 Device 负责

1. payload 接收
2. CRC 校验
3. 固定模板渲染
4. 本地设备状态读取
5. 最终整屏刷新

## 12. 异常处理规则

## 12.1 Host 侧

### 数据源失败

- 不发新 payload
- 保留旧画面

### payload 编码失败

- 直接终止本轮

### BLE 发送失败

- 终止本轮
- 不做二次自动改走位图模式

## 12.2 Device 侧

### CRC 失败

- 返回错误
- 不刷新

### 字段非法

- 返回错误
- 不刷新

### 渲染失败

- 返回刷新失败
- 保留旧画面

## 13. 测试设计

## 13.1 Host 单元验证

1. `DashboardSnapshot` 组装
2. `DashboardSnapshotV1` 编码长度固定 `192B`
3. CRC32 结果稳定
4. 模型名 ASCII 化规则
5. 无变化跳过逻辑

## 13.2 协议联调验证

1. `0x0078` START ACK
2. `0x0079` DATA ACK
3. `0x007A` COMMIT ACK
4. `0x007B` 刷新成功
5. 错误码覆盖：
   - 非法版本
   - CRC 错误
   - 长度错误
   - busy

## 13.3 固件渲染验证

1. 总量四列显示正确
2. 套餐百分比与标签显示正确
3. `>= 80%` 红色告警正确
4. 表格最多 `4` 行展示正确
5. 设备状态显示正确

## 13.4 回归验证

1. 位图模式仍可正常推图
2. 固件渲染模式可正常刷新
3. 两种模式切换只影响发送路径，不影响采集结果

## 14. 验收标准

满足下面条件即可视为设计落地成功：

1. `bitmap` 模式继续可用
2. `firmware_render` 模式可独立启用
3. Host 可稳定发送 `DashboardSnapshotV1`
4. Device 可稳定解析并绘制模板
5. 结构化模式下，BLE 传输耗时显著低于位图模式
6. 屏幕显示字段完整、颜色规则正确、无明显错位

## 15. 当前实现建议顺序

建议按这个顺序实现：

1. Host 侧统一 `DashboardSnapshot`
2. Host 侧新增 `firmware_render encoder`
3. Host 侧新增模式配置
4. 固件侧新增 `0x0078/0x0079/0x007A` 协议状态机
5. 固件侧新增 `DashboardSnapshotV1` 解析
6. 固件侧新增固定模板绘制
7. 联调 CRC、ACK、刷新完成响应
8. 最后做双模式回归验证
