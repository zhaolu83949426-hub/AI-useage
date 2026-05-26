# Token 用量看板使用文档

## 项目概述

Token 用量看板是一个基于 ESP32-N4 和 400x300 黑白红三色墨水屏的实时数据显示系统。通过 BLE 从 AIUsage HTTP API 获取 Token 使用数据，每 5 分钟自动刷新显示。

### 主要功能
- 今日 Token 总量、输入、输出、缓存统计
- GLM CodingPlan 和 GPT Plus 套餐用量（5小时/一周）
- Top 4 模型调用排行及占比
- 设备电池电量显示
- 5 分钟自动刷新 + 数据变更缓存机制

---

## 环境要求

### 硬件
- ESP32-N4 开发板（COM4 口）
- 4.2 寸 400x300 黑白红三色墨水屏
- BLE 支持的设备（用于 Host 端通信）

### 软件
- Python 3.8+
- PlatformIO（用于固件编译上传）
- AIUsage HTTP API（需在本地运行 `http://localhost:3847`）
- Windows 操作系统（自启动脚本为 PowerShell）

---

## 安装步骤

### 1. 克隆项目
```bash
cd D:\open-sprout\AI-Usage\AI-Usage
```

### 2. 安装 Python 依赖
```bash
pip install pillow requests bleak
```

### 3. 配置 GLM API Token

创建 `.glm_token` 文件（用于获取 CodingPlan 数据）：
```bash
# 文件位置：tools/token_dashboard_host/.glm_token
# 内容：你的 GLM API Key
58b7cd94bc054d7c98f20d2f2c8dbbb5.k6hVKo0VDVb0CmBV
```

或设置环境变量（不推荐，Python 进程可能读取不到）：
```powershell
[System.Environment]::SetEnvironmentVariable('TOKEN_DASHBOARD_GLM_API_TOKEN', '你的key', 'User')
```

### 4. 编译并上传固件

```bash
cd firmware
pio run --target upload
```

确保 ESP32-N4 连接到 COM4 口。如需修改串口，编辑 `firmware/platformio.ini`：
```ini
[env:esp32-n4]
upload_port = COM4
```

---

## 配置说明

### 渲染模式

编辑 `tools/token_dashboard_host/config.py`：
```python
TOKEN_DASHBOARD_RENDER_MODE = "bitmap"  # 推荐：Host 端渲染，字体清晰
# TOKEN_DASHBOARD_RENDER_MODE = "firmware_render"  # 设备端渲染，传输快
```

### 刷新间隔

```python
REFRESH_INTERVAL_SECONDS = 300  # 5 分钟，可根据需要调整
```

### 数据源

看板从以下 API 获取数据：
- AIUsage 概览：`http://localhost:3847/api/summary?range=day`
- AIUsage 模型：`http://localhost:3847/api/models?range=day`

确保 AIUsage 服务已启动。

---

## 运行方式

### 手动启动

```bash
cd D:\open-sprout\AI-Usage\AI-Usage
python -m tools.token_dashboard_host.main
```

### 设置开机自启动

运行 PowerShell 脚本（以管理员身份）：

```powershell
cd D:\open-sprout\AI-Usage\AI-Usage\tools\token_dashboard_host
.\setup_autostart.ps1
```

该脚本会创建：
1. **启动文件夹快捷方式**（当前用户）
   - 位置：`%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\Token Dashboard.lnk`
   
2. **任务计划程序任务**（更可靠，后台运行）
   - 任务名：`TokenDashboardAutoStart`
   - 触发器：用户登录时
   - 设置：允许使用电池、不停止任务、启动时立即运行

### 取消自启动

```powershell
.\remove_autostart.ps1
```

---

## 日志和调试

### 查看日志

运行时日志会输出到控制台：
```
2026-05-26 12:00:15 [INFO] dashboard: Token 用量看板 started
2026-05-26 12:00:15 [INFO] dashboard: Collecting data...
2026-05-26 12:00:19 [INFO] dashboard: Connecting to device...
2026-05-26 12:00:22 [INFO] dashboard: Device: WiFi=disconnected, battery=100%
2026-05-26 12:00:22 [INFO] dashboard: Data changed since last cycle, proceeding with update
2026-05-26 12:00:22 [INFO] dashboard: Update succeeded
```

### Debug 图片

每次更新会生成 `dashboard_debug.png`，可用图片查看器查看渲染结果。

### 缓存文件

位置：`.dashboard_cache.json`（项目根目录）

查看缓存内容：
```bash
cat .dashboard_cache.json
```

清除缓存（强制下次更新）：
```bash
del .dashboard_cache.json
```

---

## 故障排查

### 问题：数据为空，只有套餐信息

**原因**：旧进程仍在运行，使用的是 SQLite 数据源

**解决**：
```powershell
# 停止所有 Python 进程
Get-Process python | Stop-Process -Force

# 重新启动
python -m tools.token_dashboard_host.main
```

### 问题：GLM CodingPlan 数据为空

**原因**：GLM API Token 未配置

**解决**：
1. 检查 `.glm_token` 文件是否存在
2. 文件内容是否正确（仅包含 API Key，无换行）
3. 查看日志是否有 "GLM_API_TOKEN not configured" 警告

### 问题：设备连接失败

**原因**：设备未插入或 BLE 被占用

**解决**：
1. 检查 ESP32-N4 是否连接到 COM4
2. 重启设备：按 RST 按钮
3. 检查是否有其他 BLE 连接
4. 查看日志中的 BLE 设备名称（应为 "OD" 开头）

### 问题：界面显示异常

**原因**：固件与 Host 端模式不匹配

**解决**：
```bash
# 重新编译上传固件
cd firmware
pio run --target upload

# 确认 Host 端配置
# config.py 中 TOKEN_DASHBOARD_RENDER_MODE = "bitmap"
```

### 问题：自启动失败

**原因**：Task Scheduler 权限不足或路径错误

**解决**：
1. 以管理员身份运行 `setup_autostart.ps1`
2. 检查启动文件夹快捷方式是否存在
3. 手动运行命令测试是否正常

---

## API 接口说明

### AIUsage API

**概览数据**
```
GET http://localhost:3847/api/summary?range=day
```
响应：
```json
{
  "totalTokens": 88077530,
  "inputTokens": 7651407,
  "outputTokens": 222925,
  "cacheReadTokens": 80196224,
  "cacheWriteTokens": 0,
  "thinkingTokens": 6974
}
```

**模型排行**
```
GET http://localhost:3847/api/models?range=day
```
响应：
```json
{
  "models": [
    {
      "model": "glm-4.7",
      "provider": "zhipu",
      "callCount": 567,
      "totalTokens": 70193721,
      "percentage": 79.9
    }
  ]
}
```

### GLM API

**套餐查询**
```
POST https://open.bigmodel.cn/api/paas/v4/chat
Authorization: Bearer <YOUR_TOKEN>
```

---

## 文件结构

```
tools/token_dashboard_host/
├── main.py                 # 主入口
├── config.py               # 全局配置
├── collectors/
│   ├── aiusage.py         # AIUsage API 数据收集
│   ├── glm_plan.py        # GLM CodingPlan 数据收集
│   └── gpt_plan.py        # GPT Plus 套餐数据收集
├── renderer/
│   ├── dashboard.py       # PIL 渲染实现
│   ├── bitmap.py          # RGB → 黑白红位平面转换
│   └── snapshot.py        # 数据快照结构
├── ble/
│   ├── transport.py       # BLE 传输层
│   └── protocol.py        # BLE 协议定义
├── setup_autostart.ps1    # 自启动安装脚本
├── remove_autostart.ps1   # 自启动卸载脚本
└── .glm_token             # GLM API Token 配置
```

---

## 常用命令

```bash
# 启动看板
python -m tools.token_dashboard_host.main

# 停止看板
Get-Process python | Stop-Process -Force

# 查看日志
# 日志直接输出到控制台

# 清除缓存
del .dashboard_cache.json

# 测试 API
curl http://localhost:3847/api/summary?range=day
curl http://localhost:3847/api/models?range=day

# 重新上传固件
cd firmware
pio run --target upload

# 查看调试图片
# 查看项目根目录下的 dashboard_debug.png
```

---

## 联系支持

如有问题，请查看：
1. 代码注释：`tools/token_dashboard_host/*.py`
2. 设计文档：`docs/token-usage-dashboard-detailed-design.md`
3. 固件代码：`firmware/src/`
