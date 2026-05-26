"""GPT Plus plan collector from local Codex session logs."""

import glob
import json
import logging
import os
from datetime import datetime, timezone, timedelta

from ..config import CODEX_SESSIONS_DIR
from ..renderer.snapshot import GPTPlanStatus

logger = logging.getLogger(__name__)

# China Standard Time offset
_CST = timezone(timedelta(hours=8))


def collect_gpt_plan() -> GPTPlanStatus:
    """Extract GPT Plus rate limits from latest Codex session file."""
    try:
        pattern = os.path.join(CODEX_SESSIONS_DIR, "**", "rollout-*.jsonl")
        files = sorted(
            glob.glob(pattern, recursive=True),
            key=os.path.getmtime,
            reverse=True,
        )
        if not files:
            logger.warning("No Codex session files found")
            return GPTPlanStatus(
                five_hour_percent=0,
                five_hour_label="--:--",
                week_percent=0,
                week_label="--月--日",
                available=False,
            )

        # Read the latest file, search backwards for rate_limits
        for filepath in files[:3]:  # check up to 3 most recent files
            result = _search_file_for_rate_limits(filepath)
            if result:
                return result

        logger.warning("No rate_limits found in recent Codex sessions")
        return GPTPlanStatus(
            five_hour_percent=0,
            five_hour_label="--:--",
            week_percent=0,
            week_label="--月--日",
            available=False,
        )

    except Exception as e:
        logger.error(f"GPT plan collection failed: {e}")
        return GPTPlanStatus(
            five_hour_percent=0,
            five_hour_label="--:--",
            week_percent=0,
            week_label="--月--日",
            available=False,
        )


def _search_file_for_rate_limits(filepath: str) -> GPTPlanStatus | None:
    """Search a JSONL file for the latest rate_limits entry."""
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            lines = f.readlines()
    except (OSError, UnicodeDecodeError):
        return None

    # Search backwards
    for line in reversed(lines):
        line = line.strip()
        if not line:
            continue
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            continue

        rate_limits = entry.get("payload", {}).get("rate_limits")
        if not rate_limits:
            continue

        primary = rate_limits.get("primary", {})
        secondary = rate_limits.get("secondary", {})

        if not primary or not secondary:
            continue

        result = GPTPlanStatus(
            available=True,
            five_hour_percent=int(primary.get("used_percent", 0)),
            five_hour_label="--:--",
            week_percent=int(secondary.get("used_percent", 0)),
            week_label="--月--日",
        )

        # Primary = 5-hour window
        resets_at = primary.get("resets_at")
        if resets_at:
            result.five_hour_label = _format_timestamp(resets_at, is_weekly=False)

        # Secondary = weekly window
        resets_at = secondary.get("resets_at")
        if resets_at:
            result.week_label = _format_timestamp(resets_at, is_weekly=True)

        return result

    return None


def _format_timestamp(unix_seconds: int | float, is_weekly: bool) -> str:
    """Format unix timestamp to display label in local time."""
    try:
        dt = datetime.fromtimestamp(int(unix_seconds), tz=_CST)
        if is_weekly:
            return f"{dt.month}月{dt.day}日"
        return dt.strftime("%H:%M")
    except (OSError, ValueError):
        return "--:--" if not is_weekly else "--月--日"
