"""Token 用量看板 - 全局配置"""

import os

# BLE
BLE_SERVICE_UUID = "00002446-0000-1000-8000-00805f9b34fb"
BLE_CHAR_UUID = "00002446-0000-1000-8000-00805f9b34fb"
BLE_DEVICE_NAME_PREFIX = "OD"
BLE_MTU = 512
BLE_CHUNK_SIZE = 500  # per-chunk payload (MTU - opcode - margin)

# Display
DISPLAY_WIDTH = 400
DISPLAY_HEIGHT = 300
PLANE_SIZE = (DISPLAY_WIDTH * DISPLAY_HEIGHT) // 8  # 15000 bytes per plane

# Paths
AIUSAGE_DB_PATH = r"C:\Users\zhaolu\.aiusage\cache.db"
CODEX_SESSIONS_DIR = r"C:\Users\zhaolu\.codex\sessions"

# GLM API
GLM_API_BASE = "https://open.bigmodel.cn"
# Try local config file first, then env var
_glm_token_file = os.path.join(os.path.dirname(__file__), ".glm_token")
if os.path.exists(_glm_token_file):
    with open(_glm_token_file, "r") as f:
        GLM_API_TOKEN = f.read().strip()
else:
    GLM_API_TOKEN = os.getenv("TOKEN_DASHBOARD_GLM_API_TOKEN", "")

# Refresh
REFRESH_INTERVAL_SECONDS = 300  # 5 minutes

# Battery voltage -> percent mapping
BATTERY_VOLTAGE_MAX = 4.20
BATTERY_VOLTAGE_MIN = 3.30

# Alert threshold for plan progress bars
PLAN_ALERT_THRESHOLD = 80

# Render mode: "bitmap" or "firmware_render"
TOKEN_DASHBOARD_RENDER_MODE = os.getenv("TOKEN_DASHBOARD_RENDER_MODE", "firmware_render")
