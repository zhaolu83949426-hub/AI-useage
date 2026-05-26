# Token 用量看板 - 设备端渲染方案实现总结

## 已完成的工作

### Host 侧（Python）

1. **统一 DashboardSnapshot 模型** (`tools/token_dashboard_host/renderer/snapshot.py`)
   - 定义了统一的数据模型 `DashboardSnapshot`
   - 包含 `ModelUsage`、`PlanStatus`、`GLMPlanStatus`、`GPTPlanStatus`、`DeviceStatus` 等数据类
   - 支持两种渲染模式共用同一数据源

2. **Firmware Render Encoder** (`tools/token_dashboard_host/renderer/firmware_render.py`)
   - 实现了 `DashboardSnapshotV1` 二进制编码器
   - 固定 192B payload 格式
   - CRC32 校验和计算
   - 模型名 ASCII 化处理

3. **双模式支持**
   - `config.py`: 添加 `TOKEN_DASHBOARD_RENDER_MODE` 配置（bitmap/firmware_render）
   - `ble/transport.py`: 添加新的 BLE 命令（0x0078/0x0079/0x007A）和 `send_dashboard_snapshot` 方法
   - `main.py`: 实现双模式分发逻辑，根据配置选择不同的发送路径
   - 采集器更新：统一使用新的 `ModelUsage` 和 `PlanStatus` 基类

### Device 侧（C++）

4. **BLE 协议状态机** (`src/dashboard_protocol.h/cpp`)
   - 定义了协议状态：IDLE、RECEIVING、READY_TO_COMMIT、RENDERING
   - 实现了三个新命令的处理：
     - `0x0078` DASHBOARD_RENDER_START
     - `0x0079` DASHBOARD_RENDER_DATA
     - `0x007A` DASHBOARD_RENDER_COMMIT
   - CRC32 校验
   - 错误码定义和处理

5. **固定模板渲染器** (`src/dashboard_renderer.h/cpp`)
   - `DashboardDataV1` 结构体定义
   - 二进制 payload 解析函数 `dashboard_parse_v1`
   - 工具函数：格式化 token 数量、百分比等
   - 渲染框架（需要根据实际显示驱动实现具体绘制逻辑）

6. **命令分发集成**
   - `communication.cpp`: 添加新命令的分发处理
   - `communication.h`: 添加新函数声明
   - COMMIT 成功后自动调用渲染器

## 使用方法

### Host 侧

1. **使用位图模式（默认）**
   ```bash
   # 不设置环境变量，或显式设置为 bitmap
   python -m tools.token_dashboard_host.main
   ```

2. **使用固件渲染模式**
   ```bash
   # 设置环境变量
   set TOKEN_DASHBOARD_RENDER_MODE=firmware_render
   python -m tools.token_dashboard_host.main
   ```

### Device 侧

固件会自动识别并处理以下命令：
- `0x0078` - 开始接收结构化快照
- `0x0079` - 接收快照数据分块
- `0x007A` - 提交并触发渲染

## 协议格式

### START (0x0078)
```
[0x00, 0x78]
[version:1]         // 固定 1
[flags:1]           // bit0: FULL, bit1: FAST
[payload_len_le:2]  // 固定 192
[crc32_le:4]        // CRC32 校验和
```

### DATA (0x0079)
```
[0x00, 0x79]
[payload_chunk...]  // 数据分块
```

### COMMIT (0x007A)
```
[0x00, 0x7A]
[refresh_mode:1]    // 0=FULL, 1=FAST
```

## 后续工作

1. **完善渲染器实现**
   - 在 `dashboard_render` 函数中实现实际的绘制逻辑
   - 根据显示驱动 API 实现边框、文本、进度条等绘制
   - 实现颜色规则（>= 80% 使用红色）

2. **测试验证**
   - 单元测试：编码器、解析器、CRC 校验
   - 集成测试：完整的 Host-Device 通信流程
   - 回归测试：确保位图模式仍然可用

3. **优化和扩展**
   - 实现局部刷新模式（FAST）
   - 添加更多中文标签支持
   - 优化渲染性能

## 文件变更清单

### 新增文件
- `tools/token_dashboard_host/renderer/snapshot.py`
- `tools/token_dashboard_host/renderer/firmware_render.py`
- `src/dashboard_protocol.h`
- `src/dashboard_protocol.cpp`
- `src/dashboard_renderer.h`
- `src/dashboard_renderer.cpp`
- `docs/token-usage-dashboard-implementation-summary.md` (本文件)

### 修改文件
- `tools/token_dashboard_host/config.py` - 添加渲染模式配置
- `tools/token_dashboard_host/collectors/aiusage.py` - 统一使用 ModelUsage
- `tools/token_dashboard_host/collectors/glm_plan.py` - 继承统一 PlanStatus
- `tools/token_dashboard_host/collectors/gpt_plan.py` - 继承统一 PlanStatus
- `tools/token_dashboard_host/renderer/dashboard.py` - 使用统一 DeviceStatus
- `tools/token_dashboard_host/ble/transport.py` - 添加新命令和发送方法
- `tools/token_dashboard_host/main.py` - 实现双模式支持
- `src/communication.h` - 添加新函数声明
- `src/communication.cpp` - 添加新命令分发

## 注意事项

1. **第一版限制**
   - 仅支持 FULL 刷新模式
   - 固定模板，不支持通用布局
   - 中文标签需要设备内置

2. **传输性能**
   - 固件渲染模式：192B payload + 协议头 ≈ 200B
   - 位图模式：约 30KB
   - 理论传输时间：固件模式约为位图模式的 1/150

3. **兼容性**
   - 位图模式完全保留，不受影响
   - 两种模式可以通过环境变量切换
   - 不做自动探测或自动切换

## 故障排查

如果遇到问题：

1. **Host 侧**
   - 检查 `TOKEN_DASHBOARD_RENDER_MODE` 环境变量
   - 查看日志中的 "Encoded snapshot" 信息
   - 确认 CRC32 校验和是否正确

2. **Device 侧**
   - 查看串口日志中的命令处理信息
   - 确认状态机是否正确转换
   - 检查 CRC 校验是否通过

3. **通信问题**
   - 确认 BLE 连接正常
   - 检查 MTU 和 chunk 大小设置
   - 验证命令 opcode 是否正确
