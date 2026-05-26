"""aiusage API token usage collector."""

import logging
import requests

from ..renderer.snapshot import ModelUsage

logger = logging.getLogger(__name__)

# AIUsage API endpoints
AIUSAGE_API_URL = "http://localhost:3847/api/summary"
AIUSAGE_MODELS_API_URL = "http://localhost:3847/api/models"
AIUSAGE_RANGE_DAY = "day"


class TodayUsage:
    """Today's aggregated token usage (legacy, for backward compatibility)."""

    def __init__(
        self,
        total_tokens: int = 0,
        input_tokens: int = 0,
        output_tokens: int = 0,
        cache_read_tokens: int = 0,
        top_models: list[ModelUsage] | None = None,
    ):
        self.total_tokens = total_tokens
        self.input_tokens = input_tokens
        self.output_tokens = output_tokens
        self.cache_read_tokens = cache_read_tokens
        self.top_models = top_models or []


def collect_today_usage() -> TodayUsage:
    """Fetch today's token usage from AIUsage API."""
    try:
        # Fetch overview data
        response = requests.get(f"{AIUSAGE_API_URL}?range={AIUSAGE_RANGE_DAY}", timeout=10)
        response.raise_for_status()
        data = response.json()

        # Parse overview data
        result = TodayUsage(
            total_tokens=data.get("totalTokens", 0),
            input_tokens=data.get("inputTokens", 0),
            output_tokens=data.get("outputTokens", 0),
            cache_read_tokens=data.get("cacheReadTokens", 0),
        )

        # Fetch models data
        models_response = requests.get(f"{AIUSAGE_MODELS_API_URL}?range={AIUSAGE_RANGE_DAY}", timeout=10)
        models_response.raise_for_status()
        models_data = models_response.json()

        # Parse models list
        for model_data in models_data.get("models", []):
            result.top_models.append(ModelUsage(
                model=model_data["model"],
                provider=model_data.get("provider", "unknown"),
                calls=model_data.get("callCount", 0),
                total_tokens=model_data.get("totalTokens", 0),
                share_percent=model_data.get("percentage", 0.0),
            ))

        # API already returns sorted by percentage desc, take top 5
        result.top_models = result.top_models[:5]

        return result

    except Exception as e:
        logger.error(f"aiusage API collection failed: {e}")
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
