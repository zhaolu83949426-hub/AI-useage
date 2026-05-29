"""GLM CodingPlan quota collector via Zhipu API."""

import logging
from datetime import datetime, timezone, timedelta

import requests

from ..config import GLM_API_BASE, GLM_API_TOKEN
from ..renderer.snapshot import GLMPlanStatus

logger = logging.getLogger(__name__)
_CST = timezone(timedelta(hours=8))


def collect_glm_plan() -> GLMPlanStatus:
    """Fetch GLM CodingPlan usage from Zhipu API."""
    if not GLM_API_TOKEN:
        logger.warning("GLM_API_TOKEN not configured")
        return GLMPlanStatus(
            five_hour_percent=0,
            five_hour_label="--:--",
            week_percent=0,
            week_label="--/--",
            available=False,
        )

    try:
        resp = requests.get(
            f"{GLM_API_BASE}/api/monitor/usage/quota/limit",
            headers={"Authorization": f"Bearer {GLM_API_TOKEN}"},
            timeout=15,
        )
        resp.raise_for_status()
        data = resp.json()

        if data.get("code") != 200:
            logger.warning(f"GLM API returned code={data.get('code')}")
            return GLMPlanStatus(
                five_hour_percent=0,
                five_hour_label="--:--",
                week_percent=0,
                week_label="--/--",
                available=False,
            )

        level = str(data.get("data", {}).get("level", "")).strip()
        result = GLMPlanStatus(
            plan_level=level.upper() if level else None,
            available=True,
            five_hour_percent=0,
            five_hour_label="--:--",
            week_percent=0,
            week_label="--/--",
        )

        for limit in data.get("data", {}).get("limits", []):
            limit_type = limit.get("type", "")
            unit = limit.get("unit", 0)
            percentage = int(limit.get("percentage", 0))
            next_reset = limit.get("nextResetTime")

            if limit_type == "TOKENS_LIMIT" and unit == 3:
                result.five_hour_percent = percentage
                result.five_hour_label = _format_reset_time(next_reset, is_weekly=False)
            elif limit_type == "TOKENS_LIMIT" and unit == 6:
                result.week_percent = percentage
                result.week_label = _format_reset_time(next_reset, is_weekly=True)

        return result

    except Exception as e:
        logger.error(f"GLM plan collection failed: {e}")
        return GLMPlanStatus(
            five_hour_percent=0,
            five_hour_label="--:--",
            week_percent=0,
            week_label="--/--",
            available=False,
        )


def _format_reset_time(timestamp_ms: int | float | None, is_weekly: bool) -> str:
    """Format millisecond timestamp to display label."""
    if not timestamp_ms:
        return "--:--" if not is_weekly else "--/--"

    try:
        dt = datetime.fromtimestamp(int(timestamp_ms) / 1000, tz=_CST)
        if is_weekly:
            return f"{dt.month}-{dt.day}"
        return dt.strftime("%H:%M")
    except (TypeError, ValueError, OSError):
        return "--:--" if not is_weekly else "--/--"
