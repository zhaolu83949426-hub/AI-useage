# 固件概述

## 基础固件

本项目基于 **J-Calendar** (版本 1.1.9) 墨水屏日历固件，原始固件功能包括：

- 4.2 寸三色墨水屏（400×300）月历显示
- 农历、公共假期、倒计日展示
- 天气（实时/每日）、课程表
- WiFi 配置、OTA 升级、Web 配置页面
- 按钮操作（刷新/配置/清除）
- 低功耗深度睡眠

完整的固件功能介绍、硬件接线、按钮操作、Web 配置、Q&A 等内容，请直接参阅 [jcalendar-1.1.9/README.md](../jcalendar-1.1.9/README.md)。

## Token 用量看板扩展

在 J-Calendar 基础上，本项目新增了 **Token 用量看板** 功能，通过 BLE 从主机接收 AI 服务用量数据，在墨水屏上实时展示：

- 今日 Token 总量、输入、输出、缓存统计
- GLM CodingPlan 和 GPT Plus 套餐用量（5 小时 / 一周窗口）
- Top 4 模型调用排行及占比
- 设备电池电量、刷新时间
- 用量 ≥ 80% 红色告警

该扩展由两部分组成：

1. **主机端** (`tools/token_dashboard_host/`)：Python 程序，采集数据并通过 BLE 推送到设备
2. **设备端** (`jcalendar-1.1.9/src/dashboard_*.cpp`)：固件内嵌的看板渲染模块

## 固件环境

当前使用 `z21` 环境编译，对应 SES 拆机屏（丝印 A13600**）。

| 项目 | 值 |
|------|-----|
| 固件工程 | `jcalendar-1.1.9/` |
| 编译目标 | `z21`（默认环境之一） |
| 开发板 | `esp32dev` (ESP32-D0WD-V3) |
| 分区方案 | `min_spiffs.csv` |
| 屏幕分辨率 | `400 × 300` |
| 显示驱动 | `GxEPD2` 系列（由 `SI_DRIVER` 宏选择） |

其他可用环境：`z15`、`z98`、`1680`，按屏幕丝印选择（详见 [jcalendar-1.1.9/README.md](../jcalendar-1.1.9/README.md) 的 Q&A 第 4 项）。

## 设备端源码结构

```
jcalendar-1.1.9/src/
├── main.cpp                  # 入口、setup/loop
├── app_config.h              # 引脚、BLE 常量、显示参数
├── app_state.cpp/h           # 全局状态（命令队列、延迟任务）
├── ble_service.cpp/h         # BLE 初始化和广播
├── protocol.cpp/h            # BLE 命令分发与响应
├── display_service.cpp/h     # 显示驱动（位图直写 + 看板渲染）
├── dashboard_protocol.cpp/h  # 看板结构化快照协议状态机
├── dashboard_renderer.cpp/h  # 看板固定模板渲染器
└── ...                       # 日历、天气、电池等原始功能文件
```
