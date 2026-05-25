"""Dashboard layout renderer - renders 400x300 BWR dashboard to PIL Image."""

import logging
import os
from dataclasses import dataclass

from PIL import Image, ImageDraw, ImageFont

from ..collectors.aiusage import TodayUsage, format_share_percent, format_tokens
from ..collectors.glm_plan import GLMPlanStatus
from ..collectors.gpt_plan import GPTPlanStatus
from ..config import DISPLAY_HEIGHT, DISPLAY_WIDTH, PLAN_ALERT_THRESHOLD

logger = logging.getLogger(__name__)

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
RED = (255, 0, 0)

CANVAS_PADDING = 8
TITLE_BAR_Y = 31
OVERVIEW_BOX = (8, 37, 392, 102)
PLAN_BOXES = ((8, 108, 196, 182), (204, 108, 392, 182))
TABLE_BOX = (8, 188, 392, 272)
FOOTER_LINE_Y = 281

_FONT_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "fonts")
_SYSTEM_FONT_DIR = r"C:\Windows\Fonts"


@dataclass
class DeviceStatus:
    wifi_connected: bool = False
    battery_percent: int = 0
    available: bool = False


@dataclass
class DashboardData:
    usage: TodayUsage
    glm_plan: GLMPlanStatus
    gpt_plan: GPTPlanStatus
    device: DeviceStatus
    last_refresh: str = ""


def _load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    """Load a Chinese-capable font, falling back to system fonts."""
    candidates = []
    if bold:
        candidates.append(os.path.join(_SYSTEM_FONT_DIR, "msyhbd.ttc"))
    candidates.append(os.path.join(_SYSTEM_FONT_DIR, "msyh.ttc"))
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
        "title": _load_font(18, bold=True),
        "status": _load_font(9, bold=True),
        "overview_label": _load_font(9, bold=True),
        "total_value": _load_font(30, bold=True),
        "overview_value": _load_font(22, bold=True),
        "total_unit": _load_font(8, bold=True),
        "plan_title": _load_font(10, bold=True),
        "plan_label": _load_font(8, bold=True),
        "plan_pct": _load_font(10, bold=True),
        "plan_time": _load_font(8, bold=True),
        "table_header": _load_font(8, bold=True),
        "table_body": _load_font(8, bold=False),
        "footer": _load_font(9, bold=True),
    }


def render_dashboard(data: DashboardData) -> Image.Image:
    """Render full 400x300 dashboard to RGB PIL Image."""
    img = Image.new("RGB", (DISPLAY_WIDTH, DISPLAY_HEIGHT), WHITE)
    draw = ImageDraw.Draw(img)
    fonts = _load_fonts()
    _draw_title_bar(draw, data.device, fonts)
    _draw_overview(draw, data.usage, fonts)
    _draw_plan_cards(draw, data.glm_plan, data.gpt_plan, fonts)
    _draw_model_table(draw, data.usage, fonts)
    _draw_footer(draw, data.last_refresh, fonts["footer"])
    return img


def _draw_title_bar(draw: ImageDraw.ImageDraw, device: DeviceStatus, fonts: dict[str, ImageFont.FreeTypeFont]) -> None:
    """Draw the top title bar and device status area."""
    draw.text((10, 4), "Token 用量看板", fill=BLACK, font=fonts["title"])
    if device.available:
        _draw_wifi_icon(draw, 286, 11)
        draw.text((300, 7), "Wi-Fi 已连接" if device.wifi_connected else "Wi-Fi 未连接", fill=BLACK, font=fonts["status"])
        _draw_battery_icon(draw, 350, 7)
        draw.text((383, 7), f"{device.battery_percent}%", fill=BLACK, font=fonts["status"], anchor="la")
        draw.line([(392, 9), (392, 24)], fill=BLACK, width=1)
    draw.line([(8, TITLE_BAR_Y), (392, TITLE_BAR_Y)], fill=BLACK, width=1)


def _draw_wifi_icon(draw: ImageDraw.ImageDraw, x: int, y: int) -> None:
    """Draw a simple black Wi-Fi icon."""
    draw.arc((x, y, x + 10, y + 10), 215, 325, fill=BLACK, width=1)
    draw.arc((x + 2, y + 2, x + 8, y + 8), 215, 325, fill=BLACK, width=1)
    draw.ellipse((x + 4, y + 9, x + 6, y + 11), fill=BLACK)


def _draw_battery_icon(draw: ImageDraw.ImageDraw, x: int, y: int) -> None:
    """Draw the battery icon used in the title bar."""
    draw.rounded_rectangle((x, y, x + 18, y + 10), radius=2, outline=BLACK, width=1)
    draw.rectangle((x + 18, y + 3, x + 20, y + 7), outline=BLACK, width=1)


def _draw_overview(draw: ImageDraw.ImageDraw, usage: TodayUsage, fonts: dict[str, ImageFont.FreeTypeFont]) -> None:
    """Draw the today overview box with four aligned columns."""
    draw.rounded_rectangle(OVERVIEW_BOX, radius=5, outline=BLACK, width=1)
    columns = [
        ("今日总量", format_tokens(usage.total_tokens), "TOKEN"),
        ("输入", format_tokens(usage.input_tokens), ""),
        ("输出", format_tokens(usage.output_tokens), ""),
        ("缓存", format_tokens(usage.cache_read_tokens), ""),
    ]
    for index, column in enumerate(columns):
        _draw_overview_column(draw, index, column, fonts)
        if index:
            _draw_dashed_vertical(draw, 104 * index, 43, 96)


def _draw_overview_column(
    draw: ImageDraw.ImageDraw,
    index: int,
    column: tuple[str, str, str],
    fonts: dict[str, ImageFont.FreeTypeFont],
) -> None:
    """Draw one overview column and center its content."""
    left = 8 + index * 96
    center = left + 48
    label, value, unit = column
    draw.text((left + 10, 44), label, fill=BLACK, font=fonts["overview_label"])
    value_font = fonts["total_value"] if index == 0 else fonts["overview_value"]
    value_y = 73 if index == 0 else 76
    draw.text((center, value_y), value, fill=BLACK, font=value_font, anchor="mm")
    if unit:
        draw.text((center, 95), unit, fill=BLACK, font=fonts["total_unit"], anchor="mm")


def _draw_plan_cards(
    draw: ImageDraw.ImageDraw,
    glm_plan: GLMPlanStatus,
    gpt_plan: GPTPlanStatus,
    fonts: dict[str, ImageFont.FreeTypeFont],
) -> None:
    """Draw the GLM and GPT plan cards."""
    cards = [
        (PLAN_BOXES[0], "GLM CodingPlan 套餐", glm_plan),
        (PLAN_BOXES[1], "GPT Plus 套餐", gpt_plan),
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
    draw.text((left + 8, top + 7), title, fill=BLACK, font=fonts["plan_title"])
    _draw_plan_row(draw, left, top + 27, "5 小时", plan.five_hour_percent, plan.five_hour_label, fonts)
    _draw_dashed_horizontal(draw, left + 8, right - 8, top + 47)
    _draw_plan_row(draw, left, top + 49, "一周", plan.week_percent, plan.week_label, fonts)


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
    bar_x, bar_y, bar_w, bar_h = left + 42, top + 6, 66, 8
    pct_x, time_x = left + 140, left + 176
    draw.text((left + 8, top + 3), label, fill=BLACK, font=fonts["plan_label"])
    draw.rounded_rectangle((bar_x, bar_y, bar_x + bar_w, bar_y + bar_h), radius=2, outline=BLACK, width=1)
    fill_w = max(0, min(bar_w - 2, round((bar_w - 2) * percent / 100)))
    if fill_w:
        draw.rectangle((bar_x + 1, bar_y + 1, bar_x + 1 + fill_w, bar_y + bar_h - 1), fill=color)
    draw.text((pct_x, top + 7), f"{percent}%", fill=color, font=fonts["plan_pct"], anchor="mm")
    draw.text((time_x, top + 7), time_label, fill=BLACK, font=fonts["plan_time"], anchor="mm")


def _draw_model_table(draw: ImageDraw.ImageDraw, usage: TodayUsage, fonts: dict[str, ImageFont.FreeTypeFont]) -> None:
    """Draw the five-column model usage table."""
    left, top, right, bottom = TABLE_BOX
    draw.rounded_rectangle(TABLE_BOX, radius=5, outline=BLACK, width=1)
    widths = [88, 58, 42, 56, 140]
    x_positions = [left]
    for width in widths:
        x_positions.append(x_positions[-1] + width)
    _draw_table_header(draw, x_positions, top, fonts["table_header"])
    models = usage.top_models[:5]
    total_tokens = usage.total_tokens or 1
    for row_index, model in enumerate(models):
        _draw_table_row(draw, x_positions, top + 18 + row_index * 17, model, total_tokens, fonts["table_body"])
        if row_index < len(models) - 1:
            y = top + 34 + row_index * 17
            draw.line([(left, y), (right, y)], fill=BLACK, width=1)


def _draw_table_header(draw: ImageDraw.ImageDraw, positions: list[int], top: int, font: ImageFont.FreeTypeFont) -> None:
    """Draw the table header and vertical separators."""
    headers = ["模型", "提供商", "调用", "TOKEN", "占比"]
    for index, header in enumerate(headers):
        draw.text((positions[index] + 6, top + 5), header, fill=BLACK, font=font)
    draw.line([(positions[0], top + 16), (positions[-1], top + 16)], fill=BLACK, width=1)
    for x in positions[1:-1]:
        draw.line([(x, top + 16), (x, TABLE_BOX[3] - 1)], fill=BLACK, width=1)


def _draw_table_row(
    draw: ImageDraw.ImageDraw,
    positions: list[int],
    top: int,
    model,
    total_tokens: int,
    font: ImageFont.FreeTypeFont,
) -> None:
    """Draw one model table row with mini share bar."""
    draw.text((positions[0] + 6, top + 4), model.model, fill=BLACK, font=font)
    draw.text((positions[1] + 6, top + 4), model.provider, fill=BLACK, font=font)
    draw.text((positions[2] + 28, top + 4), str(model.calls), fill=BLACK, font=font, anchor="ra")
    draw.text((positions[3] + 44, top + 4), format_tokens(model.total_tokens), fill=BLACK, font=font, anchor="ra")
    share = model.total_tokens / total_tokens * 100
    _draw_share_bar(draw, positions[4] + 10, top + 5, share)
    draw.text((positions[5] - 6, top + 4), format_share_percent(share), fill=BLACK, font=font, anchor="ra")


def _draw_share_bar(draw: ImageDraw.ImageDraw, x: int, y: int, percent: float) -> None:
    """Draw the small share bar inside the table."""
    width, height = 48, 7
    draw.rounded_rectangle((x, y, x + width, y + height), radius=2, outline=BLACK, width=1)
    fill_w = max(0, min(width - 2, round((width - 2) * percent / 100)))
    if fill_w:
        draw.rectangle((x + 1, y + 1, x + 1 + fill_w, y + height - 1), fill=BLACK)


def _draw_footer(draw: ImageDraw.ImageDraw, last_refresh: str, font: ImageFont.FreeTypeFont) -> None:
    """Draw the footer separator and refresh timestamp."""
    draw.line([(8, FOOTER_LINE_Y), (392, FOOTER_LINE_Y)], fill=BLACK, width=1)
    value = last_refresh if last_refresh else "--:--"
    draw.text((12, 286), f"上次刷新：{value}", fill=BLACK, font=font)


def _draw_dashed_vertical(draw: ImageDraw.ImageDraw, x: int, start_y: int, end_y: int) -> None:
    """Draw a 1px dashed vertical separator."""
    for y in range(start_y, end_y, 4):
        draw.line([(x, y), (x, y + 1)], fill=BLACK, width=1)


def _draw_dashed_horizontal(draw: ImageDraw.ImageDraw, start_x: int, end_x: int, y: int) -> None:
    """Draw a 1px dashed horizontal separator."""
    for x in range(start_x, end_x, 4):
        draw.line([(x, y), (x + 1, y)], fill=BLACK, width=1)
