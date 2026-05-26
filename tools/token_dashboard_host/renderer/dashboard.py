"""Dashboard layout renderer - renders 400x300 BWR dashboard to PIL Image."""

import logging
import os
from datetime import datetime

from PIL import Image, ImageDraw, ImageFont

from ..collectors.aiusage import TodayUsage, format_share_percent, format_tokens
from ..renderer.snapshot import GLMPlanStatus, GPTPlanStatus, DeviceStatus
from ..config import DISPLAY_HEIGHT, DISPLAY_WIDTH, PLAN_ALERT_THRESHOLD

logger = logging.getLogger(__name__)

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
RED = (255, 0, 0)

CANVAS_PADDING = 8
OVERVIEW_BOX = (8, 8, 392, 88)
OVERVIEW_WIDTHS = (130, 85, 85, 84)
PLAN_BOXES = ((8, 93, 196, 164), (204, 93, 392, 164))
TABLE_BOX = (8, 166, 392, 272)

_FONT_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "fonts")
_SYSTEM_FONT_DIR = r"C:\Windows\Fonts"


class DashboardData:
    """Dashboard render data (legacy, for backward compatibility)."""

    def __init__(
        self,
        usage: TodayUsage,
        glm_plan: GLMPlanStatus,
        gpt_plan: GPTPlanStatus,
        device: DeviceStatus,
        last_refresh: str = "",
    ):
        self.usage = usage
        self.glm_plan = glm_plan
        self.gpt_plan = gpt_plan
        self.device = device
        self.last_refresh = last_refresh


def _load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    """Load a Chinese-capable font, optimized for E-ink display clarity."""
    candidates = []
    # 墨水屏优化：优先使用粗体字增强对比度
    candidates.append(os.path.join(_SYSTEM_FONT_DIR, "msyhbd.ttc"))  # 微软雅黑 Bold
    candidates.append(os.path.join(_SYSTEM_FONT_DIR, "simhei.ttf"))     # 黑体
    candidates.append(os.path.join(_SYSTEM_FONT_DIR, "SimHei.ttf"))
    # 思源黑体 Bold（需下载，最适合屏幕显示）
    candidates.append(os.path.join(_FONT_DIR, "SourceHanSansSC-Bold.otf"))
    candidates.append(os.path.join(_SYSTEM_FONT_DIR, "SourceHanSansSC-Bold.otf"))
    candidates.append(os.path.join(_FONT_DIR, "SourceHanSansSC-Regular.otf"))
    candidates.append(os.path.join(_SYSTEM_FONT_DIR, "SourceHanSansSC-Regular.otf"))
    # 备用系统字体
    candidates.append(os.path.join(_SYSTEM_FONT_DIR, "msyh.ttc"))
    # 检查项目 fonts 目录
    for name in os.listdir(_FONT_DIR) if os.path.isdir(_FONT_DIR) else []:
        if name.endswith((".ttf", ".otf", ".ttc")):
            candidates.append(os.path.join(_FONT_DIR, name))
    for path in candidates:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except Exception:
                continue
    logger.warning("No TrueType font found, using default")
    return ImageFont.load_default()


def _load_fonts() -> dict[str, ImageFont.FreeTypeFont]:
    """Build the font palette used across the dashboard."""
    return {
        "overview_label": _load_font(13, bold=True),
        "total_value": _load_font(28, bold=True),
        "overview_value": _load_font(22, bold=True),
        "plan_title": _load_font(12, bold=True),
        "plan_label": _load_font(10, bold=True),
        "plan_pct": _load_font(13, bold=True),
        "plan_time": _load_font(9, bold=True),
        "table_header": _load_font(13, bold=True),
        "table_body": _load_font(13, bold=False),
        "footer": _load_font(11, bold=True),
    }


def render_dashboard(data: DashboardData) -> Image.Image:
    """Render full 400x300 dashboard to RGB PIL Image."""
    img = Image.new("RGB", (DISPLAY_WIDTH, DISPLAY_HEIGHT), WHITE)
    draw = ImageDraw.Draw(img)
    fonts = _load_fonts()
    _draw_overview(draw, data.usage, fonts)
    _draw_plan_cards(draw, data.glm_plan, data.gpt_plan, fonts)
    _draw_model_table(draw, data.usage, fonts)
    # Pass battery percentage to footer
    battery_pct = data.device.battery_percent if data.device else 0
    _draw_footer(draw, data.last_refresh, battery_pct, fonts["footer"])
    return img


def _draw_overview(draw: ImageDraw.ImageDraw, usage: TodayUsage, fonts: dict[str, ImageFont.FreeTypeFont]) -> None:
    """Draw the today overview box with four aligned columns."""
    left, top, right, bottom = OVERVIEW_BOX
    draw.rounded_rectangle(OVERVIEW_BOX, radius=5, outline=BLACK, width=1)
    columns = [
        ("今日总量TOKEN", format_tokens(usage.total_tokens)),
        ("输入", format_tokens(usage.input_tokens)),
        ("输出", format_tokens(usage.output_tokens)),
        ("缓存", format_tokens(usage.cache_read_tokens)),
    ]
    current_left = left
    for index, (column, width) in enumerate(zip(columns, OVERVIEW_WIDTHS)):
        _draw_overview_column(draw, current_left, width, index, column, fonts)
        current_left += width
        if index < len(columns) - 1:
            _draw_dashed_vertical(draw, current_left, top + 8, bottom - 8)


def _draw_overview_column(
    draw: ImageDraw.ImageDraw,
    left: int,
    width: int,
    index: int,
    column: tuple[str, str],
    fonts: dict[str, ImageFont.FreeTypeFont],
) -> None:
    """Draw one overview column and center its content."""
    _, box_top, _, _ = OVERVIEW_BOX
    center = left + width // 2
    label, value = column
    draw.text((left + 10, box_top + 8), label, fill=BLACK, font=fonts["overview_label"])
    value_font = fonts["total_value"] if index == 0 else fonts["overview_value"]
    value_y = box_top + 47
    value_color = RED if index == 0 else BLACK
    draw.text((center, value_y), value, fill=value_color, font=value_font, anchor="mm")


def _draw_plan_cards(
    draw: ImageDraw.ImageDraw,
    glm_plan: GLMPlanStatus,
    gpt_plan: GPTPlanStatus,
    fonts: dict[str, ImageFont.FreeTypeFont],
) -> None:
    """Draw the GLM and GPT plan cards."""
    glm_title = "GLM CodingPlan"
    if glm_plan.plan_level:
        glm_title = f"{glm_title} {glm_plan.plan_level.title()}"
    cards = [
        (PLAN_BOXES[0], glm_title, glm_plan),
        (PLAN_BOXES[1], "GPT Plus", gpt_plan),
    ]
    for box, title, plan in cards:
        _draw_plan_card(draw, box, title, plan, fonts)


def _draw_plan_card(
    draw: ImageDraw.ImageDraw,
    box: tuple[int, int, int, int],
    title: str,
    plan: GLMPlanStatus | GPTPlanStatus,
    fonts: dict[str, ImageFont.FreeTypeFont],
) -> None:
    """Draw a plan card with title, two rows, and dashed row separator."""
    left, top, right, bottom = box
    draw.rounded_rectangle(box, radius=5, outline=BLACK, width=1)
    draw.text((left + 8, top + 6), title, fill=BLACK, font=fonts["plan_title"])
    _draw_plan_row(draw, left, top + 24, "5 小时", plan.five_hour_percent, plan.five_hour_label, fonts)
    _draw_plan_row(draw, left, top + 46, "一周", plan.week_percent, plan.week_label, fonts)


def _draw_plan_row(
    draw: ImageDraw.ImageDraw,
    left: int,
    top: int,
    label: str,
    percent: int,
    time_label: str,
    fonts: dict[str, ImageFont.FreeTypeFont],
) -> None:
    """Draw one aligned progress row inside a plan card."""
    color = RED if percent >= PLAN_ALERT_THRESHOLD else BLACK
    bar_x, bar_y, bar_w, bar_h = left + 44, top + 7, 74, 8
    pct_x = left + 136
    time_x = left + 183
    draw.text((left + 8, top + 3), label, fill=BLACK, font=fonts["plan_label"])
    draw.rounded_rectangle((bar_x, bar_y, bar_x + bar_w, bar_y + bar_h), radius=2, outline=BLACK, width=1)
    fill_w = max(0, min(bar_w - 2, round((bar_w - 2) * percent / 100)))
    if fill_w:
        draw.rectangle((bar_x + 1, bar_y + 1, bar_x + 1 + fill_w, bar_y + bar_h - 1), fill=color)
    draw.text((pct_x, top + 10), f"{percent}%", fill=color, font=fonts["plan_pct"], anchor="mm")
    draw.text((time_x, top + 10), time_label, fill=BLACK, font=fonts["plan_time"], anchor="rm")


def _draw_model_table(draw: ImageDraw.ImageDraw, usage: TodayUsage, fonts: dict[str, ImageFont.FreeTypeFont]) -> None:
    """Draw the five-column model usage table."""
    row_height = 20
    left, top, right, bottom = TABLE_BOX
    draw.rounded_rectangle(TABLE_BOX, radius=5, outline=BLACK, width=1)
    widths = [92, 56, 86, 150]
    x_positions = [left]
    for width in widths:
        x_positions.append(x_positions[-1] + width)
    header_bottom = _draw_table_header(draw, x_positions, top, fonts["table_header"])
    models = sorted(usage.top_models, key=lambda item: item.calls, reverse=True)[:4]
    total_tokens = usage.total_tokens or 1
    for row_index, model in enumerate(models):
        row_top = header_bottom + row_index * row_height
        row_center = row_top + row_height // 2
        _draw_table_row(draw, x_positions, row_center, model, total_tokens, fonts["table_body"])
        if row_index < len(models) - 1:
            y = row_top + row_height
            draw.line([(left, y), (right, y)], fill=BLACK, width=1)


def _draw_table_header(draw: ImageDraw.ImageDraw, positions: list[int], top: int, font: ImageFont.FreeTypeFont) -> int:
    """Draw the table header and vertical separators."""
    headers = ["模型", "调用", "TOKEN", "占比"]
    header_bottom = top + 21
    for index, header in enumerate(headers):
        draw.text((positions[index] + 6, top + 5), header, fill=BLACK, font=font)
    draw.line([(positions[0], header_bottom), (positions[-1], header_bottom)], fill=BLACK, width=1)
    for x in positions[1:-1]:
        draw.line([(x, header_bottom), (x, TABLE_BOX[3] - 1)], fill=BLACK, width=1)
    return header_bottom


def _draw_table_row(
    draw: ImageDraw.ImageDraw,
    positions: list[int],
    center_y: int,
    model,
    total_tokens: int,
    font: ImageFont.FreeTypeFont,
) -> None:
    """Draw one model table row with mini share bar."""
    draw.text((positions[0] + 6, center_y), model.model, fill=BLACK, font=font, anchor="lm")
    draw.text((positions[1] + 48, center_y), str(model.calls), fill=BLACK, font=font, anchor="rm")
    draw.text((positions[2] + 78, center_y), format_tokens(model.total_tokens), fill=BLACK, font=font, anchor="rm")
    share = model.total_tokens / total_tokens * 100
    _draw_share_bar(draw, positions[3] + 10, center_y - 4, share)
    draw.text((positions[4] - 6, center_y), format_share_percent(share), fill=BLACK, font=font, anchor="rm")


def _draw_share_bar(draw: ImageDraw.ImageDraw, x: int, y: int, percent: float) -> None:
    """Draw the small share bar inside the table."""
    width, height = 90, 8
    draw.rounded_rectangle((x, y, x + width, y + height), radius=2, outline=BLACK, width=1)
    fill_w = max(0, min(width - 2, round((width - 2) * percent / 100)))
    if fill_w:
        draw.rectangle((x + 1, y + 1, x + 1 + fill_w, y + height - 1), fill=BLACK)


def _draw_footer(draw: ImageDraw.ImageDraw, last_refresh: str, battery_percent: int = 0, font: ImageFont.FreeTypeFont = None) -> None:
    """Draw the footer with refresh time and battery icon progress bar."""
    value = last_refresh if last_refresh else datetime.now().strftime("%H:%M")

    # Left: refresh time
    draw.text((12, 278), f"上次刷新：{value}", fill=BLACK, font=font)

    # Right: battery icon + percentage (smaller size)
    if battery_percent >= 0:
        # Draw battery icon (reduced size)
        bat_x = DISPLAY_WIDTH - 58  # Right side
        bat_y = 282
        bat_w = 28
        bat_h = 10

        # Battery outline (rectangle with rounded corners)
        draw.rounded_rectangle((bat_x, bat_y, bat_x + bat_w, bat_y + bat_h), radius=2, outline=BLACK, width=1)

        # Battery fill (progress)
        fill_w = max(2, int((bat_w - 3) * battery_percent / 100))
        if fill_w > 2:
            draw.rectangle((bat_x + 1, bat_y + 2, bat_x + 1 + fill_w, bat_y + bat_h - 2), fill=BLACK)

        # Battery tip
        draw.rectangle((bat_x + bat_w, bat_y + 3, bat_x + bat_w + 2, bat_y + bat_h - 3), fill=BLACK)

        # Percentage text (vertically centered with battery)
        pct_text = f"{battery_percent}%"
        pct_x = bat_x + bat_w + 4
        # Use anchor for vertical centering - battery center is bat_y + bat_h/2
        pct_y = bat_y + bat_h / 2
        draw.text((pct_x, pct_y), pct_text, fill=BLACK, font=font, anchor="lm")


def _draw_dashed_vertical(draw: ImageDraw.ImageDraw, x: int, start_y: int, end_y: int) -> None:
    """Draw a 1px dashed vertical separator."""
    for y in range(start_y, end_y, 4):
        draw.line([(x, y), (x, y + 1)], fill=BLACK, width=1)


def _draw_dashed_horizontal(draw: ImageDraw.ImageDraw, start_x: int, end_x: int, y: int) -> None:
    """Draw a 1px dashed horizontal separator."""
    for x in range(start_x, end_x, 4):
        draw.line([(x, y), (x + 1, y)], fill=BLACK, width=1)
