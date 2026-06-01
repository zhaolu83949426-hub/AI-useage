# Z21 局部快刷无屏闪优化方案

## 背景

当前项目使用 `jcalendar-1.1.9` 固件驱动 400×300 黑白红三色墨水屏 Z21（`GDEQ042Z21`，控制器 `UC8276`）。现有 `firmware_render` 看板路径稳定走整屏刷新，但此前尝试过 FAST 局刷：在 Z21 驱动中加入 BW 快刷 LUT、上层计算脏区并调用局部刷新。实机现象仍有局部屏闪，因此该实现已回退。

本方案基于两份参考固件重新设计 Z21 局刷：

1. `D:\open-sprout\AI-Usage\EPD-nRF5-main-左边屏幕固件`
   - 时钟/日历固件，已实现局部快刷、视觉上无明显屏闪。
   - 关键文件：`EPD/UC81xx.c`、`EPD/EPD_service.c`、`GUI/GUI.c`。
2. 当前项目 `kindle/`
   - 网上找到的 400×300 黑白屏固件，声称实现局刷无屏闪。
   - 关键文件：`kindle/EPD.cpp`、`kindle/EPD.h`。

目标不是直接复制任一套寄存器序列，而是提炼它们能无闪局刷的共性，并按 Z21/UC8276 三色屏的约束重新实现。

## 结论

Z21 要实现稳定无屏闪局刷，应采用 **“黑白快刷子模式 + 显式旧帧/新帧双缓冲 + 严格局刷区域 + 周期性三色全刷维护”**。

不要把 Kindle 代码的 `0x24/0x26 + 0x22/0x20` 命令直接搬到 Z21。Kindle 代码适配的是另一类黑白屏命令集；Z21 当前 GxEPD2 驱动使用的是 `0x10/0x13 + 0x12` 路径。

不要只在 GxEPD2 层改 `refresh(x,y,w,h)` 为自定义 LUT。之前局刷仍有闪烁，主要风险在于：

- 旧帧/新帧没有被稳定管理；
- 上层每次重绘整页后只刷新局部，局部外的控制器 RAM/软件状态容易不同步；
- 三色 Z21 的 OTP 局刷波形本身偏全刷，需要明确切到 BW register-LUT 模式；
- FAST 刷新区域若包含红色或跨越大面积黑白变化，视觉风险高。

## 参考固件机制拆解

### EPD-nRF5 时钟固件

`EPD/EPD_service.c` 的定时刷新逻辑是：

- `ble_epd_on_timer()` 每分钟触发时钟模式刷新；
- `epd_gui_update()` 初始化屏幕、调用 `DrawGUI()` 分页绘制、再调用 `epd->drv->refresh(epd)`，最后 sleep。

`EPD/UC81xx.c` 的 UC8176 路径有几个关键点：

1. **BW 与 BWR 初始化不同**

   默认初始化中：

   ```c
   EPD_Write(UC81xx_PSR, epd->color == COLOR_BWR ? 0x0F : 0x1F);
   EPD_Write(UC81xx_CDI, epd->color == COLOR_BWR ? 0x77 : 0x97);
   ```

   这说明无闪快刷主要建立在 BW 模式上。BWR 三色模式使用的是另一套更慢、更容易闪的波形。

2. **局刷窗口严格按字节边界对齐**

   `UC81xx_SetWindow()` 会把 x 对齐到 8 像素边界，并把 x end 扩到字节边界：

   ```c
   uint16_t xe = (x + w - 1) | 0x0007;
   x &= 0xFFF8;
   EPD_Write(UC81xx_PTL, x / 256, x % 256, xe / 256, xe % 256, ...);
   ```

3. **写入图像与刷新分离**

   `UC8176_WriteImage()` 只负责写 partial window 内的 RAM：

   - BWR：`DTM1` 写黑白，`DTM2` 写颜色；
   - BW：只向 `DTM2` 写黑白数据。

   之后统一调用 `UC81xx_Refresh()` 执行刷新。

4. **时钟画面天然适配局刷**

   `GUI/GUI.c` 的 `DrawClock()` 主要是黑白数字和少量固定区域，分钟变化的实际黑白变化区域小，且不依赖红色实时变化。

### Kindle 参考固件

`kindle/EPD.cpp` 的快刷机制有三点值得借鉴：

1. **快刷初始化不是普通局刷**

   `EPD_HW_Init_Fast()` 会写温度寄存器并触发加载温度相关波形：

   ```cpp
   EPD_W21_WriteCMD(0x1A);
   EPD_W21_WriteDATA(0x6E);
   EPD_W21_WriteCMD(0x22);
   EPD_W21_WriteDATA(0x91);
   EPD_W21_WriteCMD(0x20);
   ```

   这属于该黑白屏控制器的内置快刷路径，不等同于 Z21 的三色 OTP 局刷。

2. **局刷前需要建立 base map**

   `EPD_SetRAMValue_BaseMap()` 把同一份图像写到 `0x24` 和 `0x26`，并强调“this function is necessary”。这说明无闪局刷依赖控制器中旧帧/基底帧正确。

3. **局刷只写局部 RAM 后刷新**

   `EPD_Dis_Part()` 设置 RAM 窗口，只传输局部区域的数据，再 `EPD_Part_Update()`。

### 对 Z21 的启发

两套固件的共性是：

- 快刷必须走黑白路径；
- 控制器需要可靠的旧帧/基底帧；
- 局刷窗口必须严格、稳定、按字节对齐；
- 红色内容不参与快刷；
- 需要定期全刷维护残影和红色层。

Z21 的实现应围绕这些共性设计，而不是照搬命令编号。

## 当前 Z21 驱动现状

当前驱动文件：

- `jcalendar-1.1.9/lib/GxEPD2/src/epd3c/GxEPD2_420c_Z21.cpp`
- `jcalendar-1.1.9/lib/GxEPD2/src/epd3c/GxEPD2_420c_Z21.h`

现有刷新链路：

```cpp
void GxEPD2_420c_Z21::refresh(int16_t x, int16_t y, int16_t w, int16_t h)
{
  ...
  _Init_Part();
  if (usePartialUpdateWindow) _writeCommand(0x91);
  _setPartialRamArea(x1, y1, w1, h1);
  _Update_Part();
  if (usePartialUpdateWindow) _writeCommand(0x92);
}
```

`_Init_Part()` 当前仍调用三色 OTP 初始化：

```cpp
void GxEPD2_420c_Z21::_InitDisplay()
{
  if (_hibernating) _reset();
  _writeCommand(0x00);
  _writeData(0x0f);    // LUT from OTP, 400x300
}
```

问题是：Z21 的三色 OTP 波形用于 BWR 全刷/慢刷，不适合无闪黑白局刷。仅设置 partial window 并不能保证视觉无闪。

## 设计目标

### 必须达成

1. FAST 模式下只刷新黑白内容，不刷新红色层。
2. FAST 模式下控制器旧帧和新帧始终一致可追踪。
3. 局刷区域尽量小，且不会跨越不必要的大面积区域。
4. 首帧、红色变化、布局变化、定期维护必须走 FULL。
5. Host 能知道设备是否已有局刷基线，避免直接发 FAST。
6. 失败时可自动回退 FULL，而不是卡住或留下半同步状态。

### 不追求

1. 红色像素局刷。
2. 任意图片的无闪局刷。
3. 完全消除所有残影。目标是无明显黑白闪烁，残影通过维护全刷控制。
4. 一步做到最优 LUT。LUT 需要实机调参。

## 总体架构

建议分四层实现：

```text
Host 刷新策略
  └─ 选择 FULL / FAST，并发送 refresh_mode

固件协议层
  └─ 校验 refresh_mode，记录本次请求模式

Display Service 层
  ├─ 维护 DashboardRenderState
  ├─ 根据新旧数据计算 DirtyRegion
  ├─ FULL: 建立完整三色画面 + 建立 BW 基线
  └─ FAST: 只生成 dirty rect 黑白局部 buffer 并走 Z21 BW 快刷

Z21 Driver 层
  ├─ BWR OTP full refresh
  ├─ BW register-LUT fast partial refresh
  ├─ write old frame to 0x10
  ├─ write new frame to 0x13
  └─ refresh partial window with 0x12
```

## Z21 Driver 详细设计

### 新增状态

在 `GxEPD2_420c_Z21` 中新增私有状态：

```cpp
bool _fast_bw_initialized = false;
bool _bw_baseline_valid = false;
```

如果 SRAM 足够，不建议在驱动层保存整屏 old/new buffer；驱动层只负责把调用方提供的 old/new 数据写入控制器。整屏状态由 `display_service` 管理。

### 新增方法

建议新增这些接口：

```cpp
void initFastBwMode();
void exitFastBwMode();
void writeBwPrevious(const uint8_t* black, int16_t x, int16_t y, int16_t w, int16_t h);
void writeBwNew(const uint8_t* black, int16_t x, int16_t y, int16_t w, int16_t h);
bool refreshBwWindow(int16_t x, int16_t y, int16_t w, int16_t h);
bool refreshBwDiff(const uint8_t* previous, const uint8_t* current,
                   int16_t x, int16_t y, int16_t w, int16_t h);
```

其中 `refreshBwDiff()` 是推荐上层调用的主入口：

1. 裁剪并按 8 像素对齐窗口；
2. `initFastBwMode()`；
3. `0x91` partial in；
4. `_setPartialRamArea()`；
5. `0x10` 写 previous 区域；
6. `0x13` 写 current 区域；
7. `0x12` refresh；
8. `0x92` partial out；
9. 不立刻 hibernate，交给上层统一 `finish_refresh()`。

### 为什么必须同时写 previous 和 current

此前尝试很容易出现局部屏闪，核心嫌疑是控制器 RAM 中“旧帧”不可靠。

Kindle 固件用 `EPD_SetRAMValue_BaseMap()` 先建立 base map；EPD-nRF5 在 BW 路径下每次完整绘制再刷新。Z21 若要做差分无闪，必须让控制器明确知道：

- `0x10` = 刷新前这个区域长什么样；
- `0x13` = 刷新后这个区域长什么样。

不能假设控制器在 deep sleep / hibernate / 模式切换后仍保留正确旧帧。

### Fast BW 初始化

新增 `_InitDisplay_BWFast()`，不要复用 `_Init_Part()`。

建议初版使用此前研究过的 UC8276/SE0420NQ04 register LUT 路径作为 A 方案：

```cpp
_writeCommand(0x00);
_writeData(0x3F);   // register LUT, BW mode
_writeData(0xCA);
_writeCommand(0x01); _writeData(0x03); _writeData(0x10); _writeData(0x3F); _writeData(0x3F); _writeData(0x03);
_writeCommand(0x06); _writeData(0xE7); _writeData(0xE7); _writeData(0x3D);
_writeCommand(0x60); _writeData(0x22);
_writeCommand(0x82); _writeData(0x00);
_writeCommand(0x30); _writeData(0x09);
_writeCommand(0xE3); _writeData(0x88);
_writeCommand(0x61); _writeData(0x01); _writeData(0x90); _writeData(0x01); _writeData(0x2C);
_writeCommand(0x50); _writeData(0xB7);
```

然后写入 partial LUT 到 `0x20` ~ `0x24`。

### LUT 调参策略

不要一次性把 LUT 写死为最终版本。建议定义 3 组候选：

| 名称 | 目标 | T1 | T2 | T3 | T4 | 用途 |
|---|---|---:|---:|---:|---:|---|
| conservative | 稳定优先 | 35 | 2 | 6 | 35 | 首个实机验证版本 |
| balanced | 速度/残影平衡 | 25 | 1 | 4 | 25 | 预期默认版本 |
| aggressive | 速度优先 | 18 | 1 | 3 | 18 | 后续验证 |

初版建议默认 `conservative`，因为用户当前关注的是“无屏闪”，不是极限速度。等确认不闪后再切到 `balanced`。

LUT 常量命名：

```cpp
static const unsigned char lut_20_vcom0_partial_conservative[] PROGMEM;
static const unsigned char lut_21_ww_partial_conservative[] PROGMEM;
...
```

通过编译期宏选择：

```cpp
#ifndef Z21_FAST_LUT_PROFILE
#define Z21_FAST_LUT_PROFILE Z21_FAST_LUT_CONSERVATIVE
#endif
```

### 模式恢复

FAST 刷完后不要只写 `0x00=0x0F` 就认为恢复完成。建议：

- 如果接下来继续 FAST，可保持 BW fast mode，减少初始化扰动；
- 如果要 FULL 或 hibernate，调用 `exitFastBwMode()`：
  - `_PowerOff()`；
  - `_InitDisplay()` 恢复 BWR OTP；
  - 标记 `_fast_bw_initialized=false`；
  - 后续 FULL 会重新 `_Init_Full()`。

这样避免频繁在 BWR/BW 之间切换导致残影或闪动。

## Display Service 详细设计

### 新增软件帧缓冲

Z21 400×300 单色平面大小：

```cpp
constexpr size_t kBwPlaneSize = 400 * 300 / 8; // 15000 bytes
```

建议在 `app_state` 或 `display_service.cpp` 静态区维护：

```cpp
struct DashboardRenderState {
    bool partial_baseline_ready;
    uint8_t fast_refresh_count;
    uint8_t last_battery_percent;
    DashboardDataV1 last_data;
    uint8_t previous_bw[app::kPlaneSize];
    uint8_t current_bw[app::kPlaneSize];
    uint8_t red_plane[app::kPlaneSize];
};
```

如果内存压力大，至少保留：

- `previous_bw`：上一帧黑白基线；
- `current_bw`：本次渲染结果。

`red_plane` 可只在 FULL 路径生成，FAST 不更新。

### 渲染策略

#### FULL 渲染

`render_dashboard_full(data)`：

1. 使用 GxEPD2 三色整屏路径渲染完整页面；
2. 同时生成一份软件 BW 平面作为 `previous_bw`；
3. 保存 `last_data`、`last_battery_percent`；
4. `partial_baseline_ready = true`；
5. `fast_refresh_count = 0`。

关键点：FULL 后的软件 BW 平面必须与屏幕实际黑白层一致。否则下一次 FAST 的 previous 不可信。

#### FAST 渲染

`render_dashboard_fast(data)`：

1. 如果 `partial_baseline_ready == false`，返回 false，让协议层/Host 回退 FULL；
2. 读取电池百分比；
3. 根据 `last_data` 和新 `data` 计算 dirty mask；
4. 如果 dirty 为空，更新必要状态后跳过刷新；
5. 生成本次完整 `current_bw`；
6. 把 dirty mask 转换为一个或多个刷新矩形；
7. 对每个矩形调用 Z21 `refreshBwDiff(previous_bw, current_bw, rect)`；
8. 成功后把 `current_bw` 拷贝到 `previous_bw`；
9. 更新 `last_data`、计数。

### 局刷区域不要盲目合并成一个大矩形

此前尝试把 summary/card/table 合并成一个大矩形，如果多个区域同时变化，可能接近半屏甚至大半屏，视觉上仍会像局部全闪。

建议第一版支持 **多矩形顺序局刷**，并设置上限：

```cpp
constexpr uint8_t kMaxDirtyRects = 4;
constexpr uint16_t kMaxFastArea = 400 * 120; // 超过则 FULL
```

推荐区域：

| 区域 | 坐标 | 说明 |
|---|---|---|
| Summary | `{8, 8, 384, 80}` | token 总览 + 电池 |
| GLM card | `{8, 93, 188, 71}` | GLM 进度 |
| GPT card | `{204, 93, 188, 71}` | GPT 进度 |
| Model row 0 | `{8, 188, 384, 20}` | 模型第 1 行 |
| Model row 1 | `{8, 208, 384, 20}` | 模型第 2 行 |
| Model row 2 | `{8, 228, 384, 20}` | 模型第 3 行 |
| Model row 3 | `{8, 248, 384, 20}` | 模型第 4 行 |
| Footer | `{8, 276, 384, 20}` | Last refresh，可考虑不 FAST 更新 |

优化建议：

- 表格不要整块刷新，按行刷新；
- `last_refresh` 时间不建议每 5 分钟都更新，否则 footer 永远 dirty；可在 FAST 模式隐藏或只在 FULL 更新；
- 电池变化阈值建议 ≥2% 才 dirty，避免 ADC 抖动导致每次刷 summary；
- 进度条红色阈值变化必须 FULL。

### FAST 限制条件

遇到以下情况必须 FULL：

1. 首帧或 baseline 不存在；
2. 红色内容变化；
3. 需要显示/消除红色告警；
4. dirty 总面积超过阈值；
5. dirty rect 数超过阈值；
6. 距离上次 FULL 超过 1 小时；
7. 连续 FAST 次数超过 12 次；
8. 上一次 FAST 失败；
9. 温度过低或过高（建议初版 `<10°C` 或 `>35°C` 禁用 FAST）。

## 协议和 Host 设计

### 固件协议

当前 `CMD_DASHBOARD_RENDER_COMMIT` 已有 `refresh_mode` 字节，建议保留：

```cpp
constexpr uint8_t DASHBOARD_REFRESH_MODE_FULL = 0;
constexpr uint8_t DASHBOARD_REFRESH_MODE_FAST = 1;
```

固件侧新增全局：

```cpp
uint8_t g_requestedRefreshMode = app::kRefreshModeFull;
```

`handle_dashboard_commit()` 校验后保存请求模式。`process_deferred_job()` 中：

```cpp
if (g_requestedRefreshMode == app::kRefreshModeFast) {
    ok = render_dashboard_fast(dashboard_data);
    if (!ok) ok = render_dashboard_full(dashboard_data);
} else {
    ok = render_dashboard_full(dashboard_data);
}
```

推荐固件侧也能回退 FULL，而不是只返回失败。这样 Host 不需要二次发送同一份 snapshot。

### MSD 状态位

在 MSD byte 15 增加 bit3：

```cpp
constexpr uint8_t kMsdFlagPartialBaselineReady = 1 << 3;
```

含义：

- `1`：固件已有可靠 BW baseline，Host 可以请求 FAST；
- `0`：Host 必须请求 FULL。

如果固件进入 deep sleep 后无法保证控制器 RAM，但软件 `previous_bw` 仍可靠，则 bit3 仍可为 1，因为 FAST 会显式重写 previous/current 到控制器 RAM。

### Host 刷新策略

Host 在 `firmware_render` 模式：

1. 连接设备；
2. 读取 MSD；
3. 如果 `partial_baseline_ready == false`，发送 FULL；
4. 如果跨小时，发送 FULL；
5. 如果维护计数达到阈值，发送 FULL；
6. 否则发送 FAST。

Host 不负责判断 dirty 区域，dirty 由固件判断。原因是固件才知道最终字体、布局、电池读数和红色阈值。

## 开发步骤

### 阶段 1：建立可验证的驱动级 BW 快刷

目标：不接入看板，只验证 Z21 能否通过 BW register-LUT 刷一个小矩形且不闪。

改动：

1. 在 `GxEPD2_420c_Z21.h/.cpp` 新增：
   - `_InitDisplay_BWFast()`；
   - `_Init_BWFast()`；
   - `writeBwPrevious()`；
   - `writeBwNew()`；
   - `refreshBwDiff()`。
2. 增加 conservative LUT。
3. 新增测试入口或脚本：
   - 先 FULL 白底黑字；
   - 每次只更新一个 64×32 黑白数字区域；
   - 连续 30 次；
   - 记录刷新耗时和目视闪烁。

验收：

- 小矩形刷新不出现整屏闪；
- 局部没有明显黑白反转闪；
- 连续 30 次残影可接受；
- 失败时能恢复 FULL。

### 阶段 2：接入软件 BW baseline

目标：消除控制器旧帧不可靠导致的闪烁。

改动：

1. 在 `display_service` 维护 `previous_bw/current_bw`。
2. FULL 后建立 `previous_bw`。
3. FAST 前生成完整 `current_bw`。
4. `refreshBwDiff()` 每次显式写 previous/current 区域。

验收：

- hibernate 后再次 FAST 仍不依赖控制器 RAM 残留；
- 多次 FAST 后画面不随机闪；
- baseline 丢失时自动 FULL。

### 阶段 3：看板 dirty region 精细化

目标：减少刷新面积，避免大面积局部闪。

改动：

1. Summary/card/table row/footer 分区；
2. 表格按行 dirty；
3. 电池加入阈值；
4. Footer 在 FAST 中默认不更新或合并到低频 FULL；
5. dirty 面积超过阈值直接 FULL。

验收：

- 常规 5 分钟更新只刷 1~3 个小区域；
- 进度条小变化不触发半屏刷新；
- 模型排行变化只刷变化行。

### 阶段 4：Host/协议接入 FAST

目标：形成稳定生产策略。

改动：

1. 固件支持 `refresh_mode=FAST`；
2. MSD 暴露 `partial_baseline_ready`；
3. Host 选择 FULL/FAST；
4. 固件 FAST 失败自动 FULL；
5. 每小时或 12 次 FAST 后 FULL。

验收：

- 首帧 FULL；
- 后续变化小的周期 FAST；
- 每小时 FULL；
- FAST 失败不会影响下一轮。

## 风险和规避

### 风险 1：UC8276 与参考 LUT 不完全兼容

规避：

- LUT 默认 conservative；
- 保留编译期开关切换 LUT；
- 实机记录 busy 时间；
- 如出现异常，禁用 FAST 并回退 FULL。

### 风险 2：红色层污染或不更新

规避：

- FAST 只允许黑白区域；
- 红色阈值、红色告警、红色文字变化触发 FULL；
- FULL 后重建 baseline。

### 风险 3：局刷区域太大仍有明显闪烁

规避：

- 多矩形，不做大矩形合并；
- 设置面积阈值；
- footer 不参与高频 FAST；
- 表格按行刷新。

### 风险 4：软件 baseline 与屏幕实际内容不一致

规避：

- FULL 后立即从同一渲染结果生成 baseline；
- FAST 成功后才更新 `previous_bw`；
- FAST 失败不更新 baseline，并立即 FULL；
- 任何模式切换、初始化失败、重启后 baseline 标记清零。

### 风险 5：低温快刷效果变差

规避：

- 通过 `temperatureRead()` 或控制器温度读取判断；
- 低温禁用 FAST；
- 后续可按温度选择 LUT profile。

## 推荐文件改动清单

### 固件驱动层

- `jcalendar-1.1.9/lib/GxEPD2/src/epd3c/GxEPD2_420c_Z21.h`
  - 新增 FAST BW API 和 LUT 声明。
- `jcalendar-1.1.9/lib/GxEPD2/src/epd3c/GxEPD2_420c_Z21.cpp`
  - 新增 BW fast init、LUT、previous/current 写入、`refreshBwDiff()`。

### 固件应用层

- `jcalendar-1.1.9/src/app_config.h`
  - 新增 FAST 阈值、MSD flag、温度限制。
- `jcalendar-1.1.9/src/app_state.h/.cpp`
  - 新增 `DashboardRenderState`。
- `jcalendar-1.1.9/src/display_service.h/.cpp`
  - 拆分 `render_dashboard_full()` / `render_dashboard_fast()`；
  - 增加 BW 软件渲染和 dirty region。
- `jcalendar-1.1.9/src/protocol.cpp`
  - 保存 `refresh_mode`；
  - 暴露 MSD baseline flag；
  - FAST 失败回退 FULL。
- `jcalendar-1.1.9/src/dashboard_protocol.h/.cpp`
  - 明确 FULL/FAST 常量和错误码。

### Host

- `tools/token_dashboard_host/ble/transport.py`
  - 解析 MSD bit3。
- `tools/token_dashboard_host/renderer/snapshot.py`
  - `DeviceStatus.partial_baseline_ready`。
- `tools/token_dashboard_host/main.py`
  - 选择 FULL/FAST；
  - 维护每小时 FULL 和 FAST 计数。

### 测试工具

- `tools/verify_bw_fast_refresh.py`
  - 驱动级小矩形测试；
  - 看板 FAST/FULL 对比；
  - 记录耗时。

## 验证计划

### 驱动级测试

1. 白底黑字 FULL 建立基线；
2. 只更新一个 64×32 数字区域；
3. 连续刷新 30 次；
4. 分别测试 conservative/balanced/aggressive LUT；
5. 记录：
   - busy 时间；
   - 是否局部反白闪；
   - 是否整屏闪；
   - 残影程度。

### 看板级测试

1. 首帧 FULL；
2. 只改变 total token；
3. 只改变 GLM 5H 进度；
4. 只改变 GPT 1W 标签；
5. 只改变模型排行第 2 行；
6. 同时改变多块区域；
7. 触发红色阈值；
8. 跨小时刷新。

预期：

- 1、7、8 走 FULL；
- 2~5 走小区域 FAST；
- 6 若面积超过阈值走 FULL，否则多矩形 FAST。

### 长时间测试

1. 5 分钟周期运行 24 小时；
2. 每小时 FULL；
3. 记录 FAST 成功率、平均耗时、残影；
4. 检查是否出现局部内容错位或旧内容残留。

## 关键实现伪代码

### `render_dashboard_fast()`

```cpp
bool render_dashboard_fast(const DashboardDataV1& data) {
    if (!g_dashboardRenderState.partial_baseline_ready) {
        return false;
    }

    uint8_t battery_pct = read_battery_percent();
    DirtyRectList rects = compute_dirty_rects(data, g_dashboardRenderState, battery_pct);
    if (rects.empty()) {
        update_state_without_refresh(data, battery_pct);
        return true;
    }

    if (rects.total_area() > app::kMaxFastRefreshArea || rects.count > app::kMaxFastRefreshRects) {
        return false;
    }

    render_dashboard_bw_to_plane(data, battery_pct, g_dashboardRenderState.current_bw);

    for (uint8_t i = 0; i < rects.count; i++) {
        if (!display.epd2.refreshBwDiff(
                g_dashboardRenderState.previous_bw,
                g_dashboardRenderState.current_bw,
                rects.items[i].x,
                rects.items[i].y,
                rects.items[i].w,
                rects.items[i].h)) {
            return false;
        }
    }

    memcpy(g_dashboardRenderState.previous_bw,
           g_dashboardRenderState.current_bw,
           app::kPlaneSize);
    commit_render_state(data, battery_pct, false);
    return true;
}
```

### `refreshBwDiff()`

```cpp
bool GxEPD2_420c_Z21::refreshBwDiff(
    const uint8_t* previous,
    const uint8_t* current,
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h) {

    Rect r = normalize_to_byte_aligned_screen_rect(x, y, w, h);
    if (r.empty()) return true;

    _Init_BWFast();
    _writeCommand(0x91);
    _setPartialRamArea(r.x, r.y, r.w, r.h);

    _writeCommand(0x10);
    write_plane_window(previous, r.x, r.y, r.w, r.h);

    _writeCommand(0x13);
    write_plane_window(current, r.x, r.y, r.w, r.h);

    _writeCommand(0x12);
    _waitWhileBusy("_Update_BWFast", partial_refresh_time);
    _writeCommand(0x92);
    return true;
}
```

## 开发优先级

建议按以下顺序开发：

1. `refreshBwDiff()` 驱动级小矩形测试；
2. 软件 BW baseline；
3. 看板 FULL 后建立 baseline；
4. 单矩形 FAST；
5. 多矩形 FAST；
6. Host 自动选择 FAST；
7. LUT 调参。

不要一开始同时改 Host、协议、dirty region、LUT。先证明 Z21 驱动级 BW 差分小矩形确实无闪，再接入看板。

## 最小可行版本

如果只做第一版 MVP，建议范围如下：

1. 只支持 `firmware_render` 看板；
2. FAST 只刷新 Summary、GLM card、GPT card 三个固定小区域；
3. 模型表变化直接 FULL；
4. Footer 不参与 FAST；
5. 只使用 conservative LUT；
6. 固件 FAST 失败自动 FULL；
7. 每 12 次 FAST 或每小时 FULL。

这样能最大化无闪成功率，并避免表格大区域变化带来的局部闪烁。
