# Token Dashboard 手机端方案：GitHub Gist + Flutter

## Context

当前 AI Usage 数据（token 用量、模型分布、GLM/GPT 套餐进度）由 PC 上运行的 Python host 脚本每 5 分钟采集，通过 BLE 推送到 ESP32 墨水屏。需要在手机上也能查看这些数据，支持 Android 和 iOS，展示更丰富的交互式图表。

方案：Host 侧上传数据到 GitHub 私有 Gist → Flutter App 读取 Gist JSON 渲染图表。手表暂不做。

---

## Phase 1：Host 侧 Gist 上传

### 新建文件

**`tools/token_dashboard_host/uploaders/__init__.py`** — 空包

**`tools/token_dashboard_host/uploaders/gist.py`** — 核心上传逻辑 (~80行)
- `upload_to_gist(snapshot_dict, gist_id, github_token) -> bool`
  - 先 GET 现有 Gist 内容（读取已有 history）
  - 合并新数据点、下采样到小时级、裁剪超过 7 天的旧数据
  - PATCH 更新 Gist
  - 全程 try/except，失败不影响 BLE 管线
- `_snapshot_to_history_point(snapshot_dict) -> dict` — 快照 → 缩写字段的 history 点
- `_downsample_and_prune(history, now) -> list` — 按小时去重，保留 7 天

### Gist JSON 结构

```json
{
  "schema_version": 1,
  "last_updated": "2026-06-08T17:30:31+08:00",
  "snapshot": {
    "total_tokens": 81031896,
    "input_tokens": 31812992,
    "output_tokens": 341745,
    "cache_tokens": 48715648,
    "glm_plan": { "available": true, "five_hour_percent": 39, "week_percent": 51, "..." : "..." },
    "gpt_plan": { "available": false, "..." : "..." },
    "models": [{ "model": "gpt-5.5", "provider": "openai", "calls": 349, "total_tokens": 56815366, "share_percent": 70.1 }]
  },
  "history": [
    { "ts": "2026-06-08T17:00:00+08:00", "tt": 81031896, "it": 31812992, "ot": 341745, "ct": 48715648, "g5": 39, "gw": 51, "p5": 0, "pw": 0, "m": [{"n":"gpt-5.5","p":"openai","c":349,"t":56815366}] }
  ]
}
```

- `snapshot` 用全名（只一个对象）
- `history` 用缩写字段（最多 168 条 × ~120B ≈ 20KB）
- 下采样策略：只保留整点数据（每小时 1 条），保留最近 7 天

#### History 字段对照表

| 缩写 | 全名 | 类型 |
|------|------|------|
| `ts` | timestamp | ISO 8601 string |
| `tt` | total_tokens | int |
| `it` | input_tokens | int |
| `ot` | output_tokens | int |
| `ct` | cache_tokens | int |
| `g5` | glm_5h_percent | int (0-100) |
| `gw` | glm_week_percent | int (0-100) |
| `p5` | gpt_5h_percent | int (0-100) |
| `pw` | gpt_week_percent | int (0-100) |
| `m` | models | array |
| `m[].n` | model name | string |
| `m[].p` | provider | string |
| `m[].c` | calls | int |
| `m[].t` | tokens | int |

### 修改文件

**`tools/token_dashboard_host/config.py`** — 末尾添加：
```python
GITHUB_GIST_ID = os.getenv("TOKEN_DASHBOARD_GIST_ID", "")
GITHUB_GIST_TOKEN = os.getenv("TOKEN_DASHBOARD_GIST_TOKEN", "")
```
加本地文件覆盖（同 `.glm_token` 模式）

**`tools/token_dashboard_host/main.py`** — `run_cycle()` 中 `_save_cache()` 后插入：
```python
_upload_to_gist_if_configured(cache_dict)
```
新增辅助函数 `_upload_to_gist_if_configured(cache_dict)` — 延迟导入，全包 try/except

**`.gitignore`** — 添加 `.gist_token` 和 `.gist_id`

### 新建配置文件（gitignored）
- `tools/token_dashboard_host/.gist_token` — GitHub PAT（gist scope）
- `tools/token_dashboard_host/.gist_id` — Gist ID

---

## Phase 2：Flutter App

### 项目位置
`mobile/`（`flutter create --org com.aiusage --project-name ai_usage_dashboard mobile`）

### 核心依赖
- `flutter_riverpod` — 状态管理
- `dio` — HTTP 客户端
- `fl_chart` — 图表（折线图、饼图、柱状图）
- `shared_preferences` — 离线缓存
- `intl` — 日期/数字格式化

### 目录结构
```
mobile/lib/
├── main.dart
├── app.dart
├── config/app_config.dart
├── models/
│   ├── gist_response.dart       # 顶层 JSON → GistResponse
│   ├── dashboard_snapshot.dart  # 当前快照
│   ├── plan_status.dart         # GLM/GPT 套餐状态
│   ├── model_usage.dart         # 单模型用量
│   └── history_point.dart       # 历史数据点（缩写字段解析）
├── services/
│   ├── gist_api_service.dart    # GET Gist → GistResponse
│   └── local_cache_service.dart # SharedPreferences 读写
├── providers/
│   ├── dashboard_provider.dart  # 核心：stale-while-revalidate 数据流
│   ├── history_provider.dart    # history 列表
│   └── settings_provider.dart   # Gist ID / PAT / 刷新间隔
├── screens/
│   ├── dashboard_screen.dart    # 主页：概览卡片 + 套餐进度 + 模型表
│   ├── token_trend_screen.dart  # Token 趋势折线图（4 条线）
│   ├── model_breakdown_screen.dart # 模型占比饼图 + 详情
│   ├── plan_progress_screen.dart   # 套餐进度历史图表
│   └── settings_screen.dart     # 配置 Gist ID / PAT / 刷新间隔
└── widgets/
    ├── overview_card.dart       # 2×2 Token 计数卡片
    ├── plan_card.dart           # 套餐进度条（复用 GLM/GPT）
    ├── model_table.dart         # 模型列表
    ├── token_trend_chart.dart   # fl_chart LineChart
    ├── model_pie_chart.dart     # fl_chart PieChart
    └── last_updated_footer.dart # 同步时间
```

### 数据流
```
Gist JSON → GistApiService → DashboardProvider (Riverpod)
  ├→ SharedPreferences 缓存（离线可用）
  ├→ DashboardScreen（概览）
  ├→ TokenTrendScreen（折线图）
  ├→ ModelBreakdownScreen（饼图）
  └→ PlanProgressScreen（柱状图）
```

### 离线策略：Stale-While-Revalidate
1. 启动时先读 SharedPreferences 缓存 → 立即显示
2. 后台发网络请求 → 成功则更新缓存和 UI
3. 网络失败 → 保持缓存数据，显示"离线"提示
4. 自动刷新间隔默认 15 分钟，用户可配置

### 图表规划
| 图表 | 类型 | 数据源 | 交互 |
|------|------|--------|------|
| Token 趋势 | LineChart (4线) | history[].tt/it/ot/ct | 点击查看数值，缩放时间范围 |
| 模型占比 | PieChart | snapshot.models[] | 点击扇区显示详情 |
| 套餐进度 | BarChart (分组) | history[].g5/gw/p5/pw | 按天/小时切换 |
| 套餐仪表 | CustomPaint 圆环 | snapshot.glm_plan.percent | 动画进度条 |

---

## 实施顺序

1. **Host 侧 Gist 上传** — 新建 `uploaders/gist.py`，改 `config.py` + `main.py`，手动测试
2. **Flutter 脚手架** — `flutter create`，写 model 类和 JSON 解析
3. **数据层** — GistApiService + LocalCacheService + Providers
4. **UI** — 先 Settings（配 Gist ID），再 Dashboard 主屏，最后图表页
5. **打磨** — 离线提示、自动刷新、深色模式、两端测试

## 验证

- Host 侧：运行 host 脚本后 `curl https://api.github.com/gists/{id}` 确认 JSON 更新
- Flutter：配置 Gist ID 后打开 App → 看到数据 → 断网后仍显示缓存 → 恢复网络后自动刷新
- 图表：Token 趋势图显示最近 7 天折线，模型饼图显示占比

## 注意事项

- **Gist API 限额**：带 PAT 认证 5000 次/小时，host 每 5 分钟上传 + App 每 15 分钟轮询 ≈ 20 次/小时，远在限额内
- **Gist 文件大小**：168 条历史 × ~120B ≈ 20KB，远小于 GitHub 1MB 限制
- **PAT 安全**：PAT 仅需 `gist` scope，建议使用 fine-grained token 限制权限范围
- **历史去重**：uploader 合并时会检查同一小时的已有条目，避免重复
