"""Unified DashboardSnapshot model for both bitmap and firmware_render modes."""

from dataclasses import dataclass
from typing import List


@dataclass
class ModelUsage:
    """Single model usage entry."""
    model: str
    provider: str
    calls: int
    total_tokens: int
    share_percent: float


@dataclass
class PlanStatus:
    """Base plan status."""
    available: bool
    five_hour_percent: int
    five_hour_label: str
    week_percent: int
    week_label: str


@dataclass
class GLMPlanStatus(PlanStatus):
    """GLM-specific plan status."""
    plan_level: str | None = None


@dataclass
class GPTPlanStatus(PlanStatus):
    """GPT-specific plan status (no extra fields yet)."""
    pass


@dataclass
class DeviceStatus:
    """Device hardware status."""
    wifi_connected: bool
    battery_percent: int
    available: bool


@dataclass
class DashboardSnapshot:
    """Unified dashboard data snapshot for both render modes.

    This is the single source of truth for dashboard content.
    Both bitmap renderer (PIL) and firmware_render encoder use this.
    """
    generated_at: str
    last_refresh_label: str

    # Today's token usage
    total_tokens: int
    input_tokens: int
    output_tokens: int
    cache_tokens: int

    # Plan statuses
    glm_plan: GLMPlanStatus
    gpt_plan: GPTPlanStatus

    # Top model usage (up to 4 models)
    models: List[ModelUsage]

    # Device status (only used in bitmap mode)
    device: DeviceStatus | None = None
