"""aiusage SQLite token usage collector."""

import logging
import sqlite3
from dataclasses import dataclass, field

from ..config import AIUSAGE_DB_PATH

logger = logging.getLogger(__name__)


@dataclass
class ModelUsage:
    model: str
    provider: str
    calls: int
    total_tokens: int


@dataclass
class TodayUsage:
    total_tokens: int = 0
    input_tokens: int = 0
    output_tokens: int = 0
    cache_read_tokens: int = 0
    top_models: list[ModelUsage] = field(default_factory=list)


def collect_today_usage() -> TodayUsage:
    """Read today's token usage from aiusage SQLite database."""
    try:
        conn = sqlite3.connect(f"file:{AIUSAGE_DB_PATH}?mode=ro", uri=True)
        conn.row_factory = sqlite3.Row
        cur = conn.cursor()

        # Today overview
        cur.execute("""
            SELECT
                COALESCE(SUM(input_tokens), 0) AS input_tokens,
                COALESCE(SUM(output_tokens), 0) AS output_tokens,
                COALESCE(SUM(cache_read_tokens), 0) AS cache_read_tokens,
                COALESCE(SUM(
                    input_tokens + output_tokens
                    + cache_read_tokens + cache_write_tokens + thinking_tokens
                ), 0) AS total_tokens
            FROM records
            WHERE date(ts / 1000, 'unixepoch', 'localtime') = date('now', 'localtime')
        """)
        row = cur.fetchone()
        result = TodayUsage(
            total_tokens=row["total_tokens"],
            input_tokens=row["input_tokens"],
            output_tokens=row["output_tokens"],
            cache_read_tokens=row["cache_read_tokens"],
        )

        # Top 5 models
        cur.execute("""
            SELECT
                model,
                provider,
                COUNT(*) AS calls,
                SUM(
                    input_tokens + output_tokens
                    + cache_read_tokens + cache_write_tokens + thinking_tokens
                ) AS total_tokens
            FROM records
            WHERE date(ts / 1000, 'unixepoch', 'localtime') = date('now', 'localtime')
            GROUP BY model, provider
            ORDER BY total_tokens DESC
            LIMIT 5
        """)
        for row in cur.fetchall():
            result.top_models.append(ModelUsage(
                model=row["model"],
                provider=row["provider"],
                calls=row["calls"],
                total_tokens=row["total_tokens"],
            ))

        conn.close()
        return result

    except Exception as e:
        logger.error(f"aiusage collection failed: {e}")
        return TodayUsage()


def format_tokens(count: int) -> str:
    """Format token count for display: 71.5M, 55.4K, or 999."""
    if count >= 1_000_000:
        return f"{count / 1_000_000:.1f}M"
    elif count >= 1_000:
        return f"{count / 1_000:.1f}K"
    return str(count)


def format_share_percent(value: float) -> str:
    """Format share percentage with 1 decimal."""
    return f"{value:.1f}%"
