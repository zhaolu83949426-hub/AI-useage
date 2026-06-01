"""Token 用量看板 - Main entry point with 5-minute refresh scheduler."""

import asyncio
import hashlib
import json
import logging
import os
import sys
from datetime import datetime

from .config import REFRESH_INTERVAL_SECONDS, TOKEN_DASHBOARD_RENDER_MODE
from .collectors.aiusage import collect_today_usage
from .collectors.glm_plan import collect_glm_plan
from .collectors.gpt_plan import collect_gpt_plan
from .renderer.dashboard import render_dashboard, DashboardData
from .renderer.bitmap import rgb_to_bitplanes
from .renderer.snapshot import DashboardSnapshot, DeviceStatus, ModelUsage
from .renderer.firmware_render import encode_with_crc
from .ble.transport import BLETransport

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    handlers=[logging.StreamHandler(sys.stdout)],
)
logger = logging.getLogger("dashboard")


def _build_dashboard_snapshot(
    usage,
    glm_plan,
    gpt_plan,
    device_status: DeviceStatus | None,
    last_refresh: str,
) -> DashboardSnapshot:
    """Build unified DashboardSnapshot from collected data."""
    now = datetime.now()
    generated_at = now.isoformat()

    # Convert ModelUsage to snapshot format (add share_percent if missing)
    models = []
    total_tokens = usage.total_tokens or 1
    for m in usage.top_models[:4]:
        share_percent = getattr(m, "share_percent", None)
        if share_percent is None:
            share_percent = round(m.total_tokens / total_tokens * 10000) / 100
        models.append(ModelUsage(
            model=m.model,
            provider=m.provider,
            calls=m.calls,
            total_tokens=m.total_tokens,
            share_percent=share_percent,
        ))

    return DashboardSnapshot(
        generated_at=generated_at,
        last_refresh_label=last_refresh,
        total_tokens=usage.total_tokens,
        input_tokens=usage.input_tokens,
        output_tokens=usage.output_tokens,
        cache_tokens=usage.cache_read_tokens,
        glm_plan=glm_plan,
        gpt_plan=gpt_plan,
        models=models,
        device=device_status,
    )


async def _run_bitmap_mode(transport: BLETransport, snapshot: DashboardSnapshot) -> bool:
    """Execute bitmap mode: render PIL image, send as bitplanes."""
    # Convert snapshot to legacy DashboardData format
    from .renderer.dashboard import DashboardData
    usage = type("TodayUsage", (), {
        "total_tokens": snapshot.total_tokens,
        "input_tokens": snapshot.input_tokens,
        "output_tokens": snapshot.output_tokens,
        "cache_read_tokens": snapshot.cache_tokens,
        "top_models": [type("ModelUsage", (), {
            "model": m.model,
            "provider": m.provider,
            "calls": m.calls,
            "total_tokens": m.total_tokens,
        })() for m in snapshot.models],
    })()

    data = DashboardData(
        usage=usage,
        glm_plan=snapshot.glm_plan,
        gpt_plan=snapshot.gpt_plan,
        device=snapshot.device,
        last_refresh=snapshot.last_refresh_label,
    )

    # Render dashboard
    img = render_dashboard(data)

    # Save debug PNG
    debug_path = "dashboard_debug.png"
    img.save(debug_path)
    logger.info(f"Debug image saved to {debug_path}")

    # Convert to bitplanes
    black_plane, red_plane = rgb_to_bitplanes(img)

    # Send via BLE
    logger.info("Sending bitmap image to display...")
    return await transport.send_3color_image(black_plane, red_plane)


async def _run_firmware_render_mode(transport: BLETransport, snapshot: DashboardSnapshot, refresh_mode: str) -> bool:
    """Execute firmware_render mode: encode snapshot, send structured data."""
    # Encode snapshot to binary format
    payload, crc32 = encode_with_crc(snapshot)
    logger.info(f"Encoded snapshot: {len(payload)} bytes, CRC32={crc32:08X}")

    # Send via BLE
    logger.info(f"Sending dashboard snapshot for firmware rendering ({refresh_mode})...")
    return await transport.send_dashboard_snapshot(payload, crc32, refresh_mode=refresh_mode)


# Cache file path
_CACHE_FILE = os.path.join(os.path.dirname(os.path.dirname(__file__)), ".dashboard_cache.json")
_CACHE_VERSION = "v2"
_FAST_REFRESH_COUNT = 0
_LAST_FULL_REFRESH_HOUR: str | None = None


def _snapshot_to_cache_dict(snapshot: DashboardSnapshot) -> dict:
    """Convert snapshot to dict for caching (exclude device status)."""
    return {
        "version": _CACHE_VERSION,
        "total_tokens": snapshot.total_tokens,
        "input_tokens": snapshot.input_tokens,
        "output_tokens": snapshot.output_tokens,
        "cache_tokens": snapshot.cache_tokens,
        "glm_plan": {
            "available": snapshot.glm_plan.available,
            "five_hour_percent": snapshot.glm_plan.five_hour_percent,
            "week_percent": snapshot.glm_plan.week_percent,
            "five_hour_label": snapshot.glm_plan.five_hour_label,
            "week_label": snapshot.glm_plan.week_label,
        },
        "gpt_plan": {
            "available": snapshot.gpt_plan.available,
            "five_hour_percent": snapshot.gpt_plan.five_hour_percent,
            "week_percent": snapshot.gpt_plan.week_percent,
            "five_hour_label": snapshot.gpt_plan.five_hour_label,
            "week_label": snapshot.gpt_plan.week_label,
        },
        "models": [
            {
                "model": m.model,
                "calls": m.calls,
                "total_tokens": m.total_tokens,
                "share_percent": m.share_percent,
            }
            for m in snapshot.models
        ],
    }


def _compute_cache_hash(cache_dict: dict) -> str:
    """Compute hash of cache dict for comparison."""
    # Sort keys for consistent hash, convert to JSON
    cache_str = json.dumps(cache_dict, sort_keys=True)
    return hashlib.md5(cache_str.encode()).hexdigest()


def _load_last_cache() -> dict | None:
    """Load last cache from file."""
    if not os.path.exists(_CACHE_FILE):
        return None
    try:
        with open(_CACHE_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def _save_cache(cache_dict: dict, cache_hash: str) -> None:
    """Save cache to file."""
    try:
        cache_dict["hash"] = cache_hash
        cache_dict["timestamp"] = datetime.now().isoformat()
        with open(_CACHE_FILE, "w", encoding="utf-8") as f:
            json.dump(cache_dict, f, ensure_ascii=False, indent=2)
    except Exception:
        pass


def _select_refresh_mode(device_status: DeviceStatus, now: datetime) -> str:
    global _FAST_REFRESH_COUNT, _LAST_FULL_REFRESH_HOUR

    hour_key = now.strftime("%Y-%m-%dT%H")
    logger.info(f"MSD partial_baseline_ready={int(device_status.partial_baseline_ready)}")
    if not device_status.partial_baseline_ready:
        return "FULL"
    if _LAST_FULL_REFRESH_HOUR != hour_key:
        logger.info("Hourly full refresh triggered")
        return "FULL"
    if _FAST_REFRESH_COUNT >= 12:
        logger.info("Maintenance full refresh triggered")
        return "FULL"
    return "FAST"


def _record_refresh_result(refresh_mode: str, success: bool, now: datetime) -> None:
    global _FAST_REFRESH_COUNT, _LAST_FULL_REFRESH_HOUR

    if not success:
        return
    if refresh_mode == "FULL":
        _FAST_REFRESH_COUNT = 0
        _LAST_FULL_REFRESH_HOUR = now.strftime("%Y-%m-%dT%H")
        return
    _FAST_REFRESH_COUNT += 1


async def run_cycle(transport: BLETransport) -> None:
    """Execute one complete collect-render-send cycle with cache check."""
    # 1. Collect data
    logger.info("Collecting data...")
    usage = collect_today_usage()
    glm_plan = collect_glm_plan()
    gpt_plan = collect_gpt_plan()

    # 2. Check plan availability (required by design doc)
    if not glm_plan.available and not gpt_plan.available:
        logger.warning("Both plans unavailable, skipping this cycle")
        return

    # 3. Connect to device and read status
    device_status = None
    refresh_mode = "FULL"
    if TOKEN_DASHBOARD_RENDER_MODE == "bitmap":
        logger.info("Connecting to device...")
        await transport.connect()
        device_status = await transport.read_device_status()
        logger.info(f"Device: WiFi={'connected' if device_status.wifi_connected else 'disconnected'}, "
                    f"battery={device_status.battery_percent}%")
    elif TOKEN_DASHBOARD_RENDER_MODE == "firmware_render":
        logger.info("Connecting to device...")
        await transport.connect()
        device_status = await transport.read_device_status()
        logger.info(f"Device: WiFi={'connected' if device_status.wifi_connected else 'disconnected'}, "
                    f"battery={device_status.battery_percent}%")

    # 4. Build unified snapshot
    now = datetime.now()
    if TOKEN_DASHBOARD_RENDER_MODE == "firmware_render" and device_status:
        refresh_mode = _select_refresh_mode(device_status, now)
        logger.info(f"Selected refresh mode: {refresh_mode}")
    last_refresh = now.strftime("%H:%M")
    snapshot = _build_dashboard_snapshot(usage, glm_plan, gpt_plan, device_status, last_refresh)

    # 5. Cache check: compare with last cycle
    cache_dict = _snapshot_to_cache_dict(snapshot)
    cache_hash = _compute_cache_hash(cache_dict)
    last_cache = _load_last_cache()

    if last_cache and last_cache.get("hash") == cache_hash and refresh_mode != "FULL":
        logger.info("Data unchanged from last cycle, skipping update")
        return

    if last_cache and last_cache.get("hash") == cache_hash:
        logger.info("Data unchanged, proceeding with scheduled full refresh")
    else:
        logger.info("Data changed since last cycle, proceeding with update")
    _save_cache(cache_dict, cache_hash)

    # 6. Send based on render mode
    if TOKEN_DASHBOARD_RENDER_MODE == "bitmap":
        success = await _run_bitmap_mode(transport, snapshot)
    elif TOKEN_DASHBOARD_RENDER_MODE == "firmware_render":
        success = await _run_firmware_render_mode(transport, snapshot, refresh_mode)
    else:
        logger.error(f"Unknown render mode: {TOKEN_DASHBOARD_RENDER_MODE}")
        return

    logger.info(f"Update {'succeeded' if success else 'failed'}")
    if TOKEN_DASHBOARD_RENDER_MODE == "firmware_render":
        _record_refresh_result(refresh_mode, success, now)


def calculate_next_aligned_time():
    """Calculate next 5-minute mark + 5 second delay and wait seconds."""
    now = datetime.now()
    next_5_min = ((now.minute // 5) + 1) * 5
    if next_5_min >= 60:
        target = now.replace(hour=now.hour + 1, minute=0, second=5, microsecond=0)
    else:
        target = now.replace(minute=next_5_min, second=5, microsecond=0)
    wait_seconds = (target - now).total_seconds()
    return target, wait_seconds


async def main_loop() -> None:
    """Main event loop with 5-minute refresh aligned to marks + 5s delay."""
    logger.info("Token 用量看板 started")
    logger.info("Refresh aligned to 5-minute marks + 5s delay")

    transport = BLETransport()

    # Run first cycle immediately
    try:
        await run_cycle(transport)
    except Exception:
        logger.error("First cycle failed", exc_info=True)
    finally:
        if transport.client and transport.client.is_connected:
            await transport.disconnect()

    # Subsequent cycles: align to 5-minute marks + 5s delay
    while True:
        next_time, wait_seconds = calculate_next_aligned_time()
        logger.info(f"Next update at {next_time.strftime('%H:%M:%S')} (waiting {wait_seconds:.1f}s)")
        await asyncio.sleep(wait_seconds)
        try:
            await run_cycle(transport)
        except Exception:
            logger.error("Cycle failed", exc_info=True)
        finally:
            if transport.client and transport.client.is_connected:
                await transport.disconnect()


def main():
    """Entry point."""
    try:
        asyncio.run(main_loop())
    except KeyboardInterrupt:
        logger.info("Stopped by user")


if __name__ == "__main__":
    main()
