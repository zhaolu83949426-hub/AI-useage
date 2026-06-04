# Token Dashboard Z21 局部刷新详细设计

## 1. 结论

当前这套 `jcalendar-1.1.9` 的 `z21` 固件，**底层驱动具备局部窗口刷新能力，但主线固件并没有启用**，所以你现在看到的仍然是每次提交看板数据都走整屏刷新。

同时要先说明一个现实约束：`GxEPD2_420c_Z21` 里的 `full_refresh_time` 和 `partial_refresh_time` 目前都配置为 `16000ms`，并没有单独的快刷 LUT，因此本方案的目标是：

1. 把“每 5 分钟整屏闪一次”改成“只刷新变更区域”
2. 显著降低整屏闪烁感
3. 不把“物理刷新时间大幅缩短”当成本方案承诺

本方案只覆盖 **Token Dashboard 的 `firmware_render` 主线**，不改回旧日历 UI，也不扩展到 `z15 / z98 / 1680`。

## 2. 现状核查

### 2.1 `z21` 驱动本身支持局部窗口刷新

当前驱动选择来自：

- `jcalendar-1.1.9/platformio.ini`
- `jcalendar-1.1.9/include/GxEPD2_display_selection_new_style.h`

`z21` 环境实际绑定的是 `GxEPD2_420c_Z21`。

`jcalendar-1.1.9/lib/GxEPD2/src/epd3c/GxEPD2_420c_Z21.h` 已明确声明：

- `hasPartialUpdate = true`
- `usePartialUpdateWindow = true`
- `partial_refresh_time = 16000`

`jcalendar-1.1.9/lib/GxEPD2/src/epd3c/GxEPD2_420c_Z21.cpp` 也已经实现了：

- `refresh(int16_t x, int16_t y, int16_t w, int16_t h)`
- `0x91 partial in`
- `0x90 partial window`
- `0x92 partial out`

所以结论不是“硬件完全不支持局刷”，而是“**驱动层能做区域刷新，但上层业务没有接上**”。

### 2.2 当前主线固件已经不是旧日历入口

`platformio.ini` 的 `dashboard_src_filter` 只编译：

- `main.cpp`
- `protocol.cpp`
- `display_service.cpp`
- `dashboard_protocol.cpp`
- `dashboard_renderer.cpp`

也就是说，当前 `z21` 固件真正跑的是 `OpenDisplay Dashboard` 这条链路，不是 `screen_ink.cpp` 那套老日历任务。

### 2.3 当前看板链路强制整屏刷新

现状有三个硬阻断点：

1. `jcalendar-1.1.9/src/display_service.cpp`
   - `render_dashboard()` 固定 `setFullWindow() + firstPage() + nextPage()`
2. `jcalendar-1.1.9/src/dashboard_protocol.cpp`
   - `refresh_mode != 0` 直接返回 `DASHBOARD_ERR_REFRESH_MODE_UNSUPPORTED`
3. `tools/token_dashboard_host/main.py`
   - `send_dashboard_snapshot(..., refresh_mode=\"FULL\")`

所以即使底层驱动能局刷，当前协议和显示服务也不会让它发生。

### 2.4 Host 端确实按 5 分钟节奏推送

`tools/token_dashboard_host/config.py` 当前配置：

```python
REFRESH_INTERVAL_SECONDS = 300
TOKEN_DASHBOARD_RENDER_MODE = "firmware_render"
```

这就是“每 5 分钟刷新一次”的直接来源。

## 3. 设计目标

本方案采用一个直接实现：

**在 `z21 + firmware_render` 路径下，基于现有 `GxEPD2_420c_Z21` 的 partial window 能力，新增“单次提交只刷新脏区域并一次性提交”的局部刷新模式。**

### 3.1 目标

1. `0x007A` 提交命令支持 `FAST` 模式
2. 设备根据新旧看板数据自动计算脏区域
3. 单次刷新只提交一个脏区域并触发一次 partial refresh
4. 首次渲染、维护性渲染仍允许明确走 full refresh

### 3.2 非目标

1. 不尝试本次就做“1 秒级快刷”
2. 不改 `bitmap` 模式
3. 不给 `z15 / z98 / 1680` 开放同样能力
4. 不恢复旧 `screen_ink.cpp` 入口

## 4. 总体方案

### 4.1 模式边界

- 仅 `SI_DRIVER == 21` 时开启 `FAST`
- 仅 `firmware_render` 模式支持 `FAST`
- 其他环境和其他渲染模式继续保持 `FULL`

### 4.2 刷新策略

每轮刷新分两种：

1. `FULL`
   - 冷启动后的首帧
   - Host 明确要求整刷
   - 每 12 次 `FAST` 后做一次维护整刷
   - 跨天首帧
2. `FAST`
   - 已建立局刷基线
   - 仍在同一自然日内
   - 未达到维护整刷阈值

### 4.3 核心思路

设备保存上一帧 `DashboardDataV1` 与上次电池百分比，收到新 payload 后：

1. 对比新旧数据，计算哪些逻辑区块发生变化
2. 把所有脏区块合并成一个外接矩形
3. 对该矩形执行一次 `setPartialWindow()`
4. 在这个 partial window 内复用现有 `draw_dashboard_page()` 绘制
5. 一次 `nextPage()` 完成后只触发一次 `epd2.refresh(x, y, w, h)`

这样可以避免“每个小区域各刷一次”导致的多次 `16000ms` 波形等待。

## 5. 布局调整

为了避免每次都因为底部 `LAST:` 文本变化而把最底部区域也拉进脏矩形，本方案同步做一个小布局调整：

1. 删除底部独立 `LAST: hh:mm` 显示
2. 电池百分比从底部右侧移动到顶部概览框右上角
3. 顶部概览框保留主要统计信息，底部 28px 留白，作为长期静态区域

这样做的目的不是美化，而是减少“刷新时间标签永远变化”带来的脏区扩大。

## 6. 脏区模型

### 6.1 区块划分

按当前看板布局拆成 4 个逻辑区块：

| 区块 | 坐标 |
|------|------|
| `SUMMARY` | `x=8, y=8, w=384, h=80` |
| `GLM_CARD` | `x=8, y=93, w=188, h=71` |
| `GPT_CARD` | `x=204, y=93, w=188, h=71` |
| `MODEL_TABLE` | `x=8, y=166, w=384, h=106` |

### 6.2 判脏规则

#### `SUMMARY`

以下任一变化即判脏：

- `total_tokens`
- `input_tokens`
- `output_tokens`
- `cache_tokens`
- `battery_percent`

#### `GLM_CARD`

以下任一变化即判脏：

- `glm_level`
- `glm_5h_percent`
- `glm_week_percent`
- `glm_5h_label`
- `glm_week_label`

#### `GPT_CARD`

以下任一变化即判脏：

- `gpt_5h_percent`
- `gpt_week_percent`
- `gpt_5h_label`
- `gpt_week_label`

#### `MODEL_TABLE`

以下任一变化即判脏：

- `row_count`
- 任一模型行的 `model / provider / calls / total_tokens / share_bp`

### 6.3 脏区合并

设备把所有脏区块求一个外接矩形：

```text
min_x = min(region.x)
min_y = min(region.y)
max_x = max(region.x + region.w)
max_y = max(region.y + region.h)
```

之后按 `GxEPD2` 的要求做 8 像素对齐：

- `x` 向下对齐到 8
- `w` 向上补齐到 8 的倍数

如果没有任何脏区块：

- 直接返回成功响应
- 不触发墨水屏刷新

## 7. 固件改动设计

### 7.1 `app_config.h`

新增：

```cpp
constexpr uint8_t kFirmwareMinor = 3;
constexpr uint8_t kRefreshModeFast = 1;
constexpr uint8_t kMsdFlagPartialBaselineReady = 1 << 3;
constexpr uint8_t kFastRefreshMaintenanceInterval = 12;
```

说明：

- 固件次版本升到 `0.3`
- `bit3` 用于暴露“局刷基线已建立”

### 7.2 `app_state.h / app_state.cpp`

新增一个局刷运行态：

```cpp
struct DashboardRenderState {
    bool partial_baseline_ready = false;
    uint8_t fast_refresh_count = 0;
    uint8_t last_battery_percent = 0;
    DashboardDataV1 last_data = {};
};
```

全局新增：

```cpp
extern DashboardRenderState g_dashboardRenderState;
extern uint8_t g_requestedRefreshMode;
```

职责：

1. 保存上一帧渲染数据
2. 保存局刷基线状态
3. 保存最近一次电池百分比
4. 保存本轮 Host 请求的刷新模式

### 7.3 `protocol.cpp`

修改点：

1. `handle_dashboard_commit()` 不再把 `refresh_mode != 0` 当成固定错误
2. 把请求模式写入 `g_requestedRefreshMode`
3. `process_deferred_job()` 在解析 `DashboardDataV1` 后，根据 `g_requestedRefreshMode` 调用：

```cpp
render_dashboard_full(data)
render_dashboard_fast(data)
```

4. `refresh_msd_payload()` 在状态字节中写入 `partial_baseline_ready`

### 7.4 `dashboard_protocol.cpp`

协议层只做能力校验，不做渲染策略判断。

规则改成：

1. `FULL(0)` 永远允许
2. `FAST(1)` 仅在 `SI_DRIVER == 21` 时允许
3. 其他值仍返回 `DASHBOARD_ERR_REFRESH_MODE_UNSUPPORTED`

这样协议边界清晰，渲染是否真的走局刷由显示服务决定。

### 7.5 `display_service.h`

现有接口拆成两个显式入口：

```cpp
bool render_dashboard_full(const DashboardDataV1& data);
bool render_dashboard_fast(const DashboardDataV1& data);
```

删除原来语义模糊的：

```cpp
bool render_dashboard(const DashboardDataV1& data);
```

### 7.6 `display_service.cpp`

这是本次固件改造的核心文件。

#### 新增常量

定义 4 个脏区矩形常量，和一个区块掩码枚举：

```cpp
enum DashboardDirtyRegion : uint8_t {
    DirtyNone = 0,
    DirtySummary = 1 << 0,
    DirtyGlmCard = 1 << 1,
    DirtyGptCard = 1 << 2,
    DirtyModelTable = 1 << 3,
};
```

#### 新增辅助函数

1. `uint8_t read_battery_percent()`
   - 继续复用现有实现
2. `uint8_t compute_dirty_mask(const DashboardDataV1& current, const DashboardRenderState& state, uint8_t battery_percent)`
3. `Rect union_dirty_regions(uint8_t dirty_mask)`
4. `void draw_dashboard_page_partial(const DashboardDataV1& data)`
   - 直接复用现有 `draw_dashboard_page()`，不再拆一套局刷专用绘制代码
5. `void commit_render_state(const DashboardDataV1& data, uint8_t battery_percent, bool full_refresh)`

#### 现有绘制函数调整

1. `draw_overview()`
   - 在右上角增加 `BAT:xx%`
2. `draw_footer()`
   - 删除，不再单独绘制底部 `LAST`
3. `draw_dashboard_page()`
   - 继续作为唯一页面绘制入口
   - 内部改为 `fillScreen + draw_overview + draw_plan_cards + draw_model_table`

这样做的目的，是保证 **full / fast 两条路径都复用同一套布局函数**，避免后续出现两套模板逐渐漂移。

#### `render_dashboard_full()`

保留现有整刷逻辑：

```cpp
display.setFullWindow();
display.firstPage();
do {
    draw_dashboard_page(data);
} while (display.nextPage());
finish_refresh();
```

完成后：

1. 更新 `g_dashboardRenderState.last_data`
2. 更新 `last_battery_percent`
3. `partial_baseline_ready = true`
4. `fast_refresh_count = 0`

#### `render_dashboard_fast()`

流程固定为：

1. 读取当前电池百分比
2. 如果 `partial_baseline_ready == false`，直接返回失败
3. 计算 `dirty_mask`
4. 如果 `dirty_mask == 0`，直接返回成功
5. 计算 union rect
6. `display.setPartialWindow(rect.x, rect.y, rect.w, rect.h)`
7. `display.firstPage()`
8. 在 `do/while(nextPage())` 中继续调用同一个 `draw_dashboard_page(data)`
9. `finish_refresh()`
10. 更新 `last_data / last_battery_percent / fast_refresh_count`

这里不单独写“画局部版 summary / table / card”，因为 `GxEPD2_3C` 在 partial window 模式下本身就会做裁剪，直接复用原始绘制函数最稳。

### 7.7 `ble_service.cpp`

本方案不再在连接瞬间篡改“是否可局刷”的状态。

`g_rebootFlag = false;` 的清理时机保持原样即可，但不能把 `partial_baseline_ready` 绑在 BLE 连接回调里。

局刷基线只能在 **成功完成一次 full render 后** 设置为 `true`。

## 8. Host 端改动设计

### 8.1 `tools/token_dashboard_host/main.py`

当前 `firmware_render` 模式虽然不画位图，但仍必须在发送前读取一次 `READ_MSD`。

改成：

1. 连接设备
2. 读取 `READ_MSD`
3. 判断 `partial_baseline_ready`
4. 决定本轮发 `FULL` 还是 `FAST`

刷新模式选择规则：

```text
if partial_baseline_ready == 0:
    FULL
elif local_fast_refresh_count >= 12:
    FULL
elif date_changed:
    FULL
else:
    FAST
```

### 8.2 `tools/token_dashboard_host/ble/transport.py`

保留现有协议结构，只修改调用参数：

```python
await transport.send_dashboard_snapshot(payload, crc32, refresh_mode="FAST")
```

提交包仍然是：

```text
[0x00, 0x7A, refresh_mode]
```

不扩 payload，不增加第二套局刷协议。

### 8.3 `tools/token_dashboard_host/config.py`

保留：

```python
REFRESH_INTERVAL_SECONDS = 300
```

不把刷新间隔和局刷耦合。

本方案解决的是“显示方式”，不是“采集频率”。

## 9. 协议与状态字节调整

### 9.1 `0x007A`

提交命令正式支持：

- `0` = `FULL`
- `1` = `FAST`

### 9.2 `READ_MSD` 状态字节

沿用现有 1 字节状态位，新增：

- `bit3 = partial_baseline_ready`

这样 Host 无需额外命令就能知道设备是否已经具备局刷基线。

## 10. 日志要求

固件串口日志新增以下关键点：

1. `dashboard refresh mode: FULL`
2. `dashboard refresh mode: FAST`
3. `dashboard dirty mask: 0x0?`
4. `dashboard partial rect: x=?, y=?, w=?, h=?`
5. `dashboard fast skipped: no dirty region`

Host 日志新增：

1. `MSD partial_baseline_ready=0/1`
2. `Selected refresh mode: FULL/FAST`
3. `Maintenance full refresh triggered`

## 11. 验证方案

### 11.1 静态验证

1. `pio run -d .\\jcalendar-1.1.9 -e z21`
2. 检查 `0x007A` 的 `FAST` 提交不再返回 `0x08`
3. 检查 `READ_MSD` 的 `bit3` 是否随 full render 建立

### 11.2 联调验证

联调按下面顺序做：

1. 冷启动设备
2. Host 首帧发送 `FULL`
3. 第二帧仅修改一个模型行
4. 观察串口日志中的 partial rect 是否只覆盖 `MODEL_TABLE`
5. 再修改总量统计
6. 观察 partial rect 是否扩展到 `SUMMARY + MODEL_TABLE` 的外接矩形

### 11.3 视觉验证

重点确认：

1. 不再整屏黑白闪
2. 未变区域保持静止
3. 连续 12 次 `FAST` 后第 13 次会明确整刷
4. 跨天首帧会明确整刷

## 12. 风险与边界

### 12.1 本方案不能承诺“显著更快”

原因很直接：

- `GxEPD2_420c_Z21` 当前 `partial_refresh_time == full_refresh_time == 16000ms`
- 该驱动没有专门的 fast partial LUT

所以本方案的收益主要是：

1. 降低整屏闪烁
2. 降低对未变区域的视觉干扰
3. 保持协议和现有布局代码改动最小

### 12.2 不做多块分次局刷

当前 `z21` 的 partial refresh 物理耗时并不短，如果把 `SUMMARY / CARD / TABLE` 分成多次刷，实际体验只会更差。

因此本方案明确规定：

- **每轮最多触发一次 partial refresh**
- 多块变化统一合并成一个矩形

### 12.3 后续如要追求“真快刷”，必须继续下探驱动

如果后面目标变成“接近黑白快刷的速度”，那就不是这一版方案的范围了，必须继续研究：

1. `UC8276` 是否存在可用的局刷 LUT
2. `GxEPD2_420c_Z21` 是否需要像 `GxEPD2_420_SE0420NQ04` 那样区分 `_Init_Full()` 与 `_Init_Part()` 的波形装载
3. 黑白内容是否能走 3C 面板的专用黑白刷新路径

本次设计不做这部分扩展。

## 13. 需要修改的文件清单

固件侧：

- `jcalendar-1.1.9/src/app_config.h`
- `jcalendar-1.1.9/src/app_state.h`
- `jcalendar-1.1.9/src/app_state.cpp`
- `jcalendar-1.1.9/src/protocol.cpp`
- `jcalendar-1.1.9/src/dashboard_protocol.cpp`
- `jcalendar-1.1.9/src/display_service.h`
- `jcalendar-1.1.9/src/display_service.cpp`

Host 侧：

- `tools/token_dashboard_host/main.py`
- `tools/token_dashboard_host/ble/transport.py`

文档侧：

- `docs/command-protocol.md`
- `docs/token-usage-dashboard-guide.md`
- `docs/token-usage-dashboard-detailed-design.md`
