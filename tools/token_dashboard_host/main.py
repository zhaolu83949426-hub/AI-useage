"""Token 用量看板 - Main entry point with 5-minute refresh scheduler."""

import asyncio
import hashlib
import json
import logging
import os
import sys
from dataclasses import dataclass
from datetime import datetime
from typing import Any

from .config import (
    BLE_SCAN_TIMEOUT_SECONDS,
    COLLECT_INTERVAL_SECONDS,
    DEVICE_WAKE_INTERVAL_SECONDS,
    POST_DEVICE_HANDLED_GUARD_SECONDS,
    TOKEN_DASHBOARD_RENDER_MODE,
)
from .collectors.aiusage import collect_today_usage
from .collectors.glm_plan import collect_glm_plan
from .collectors.gpt_plan import collect_gpt_plan
from .renderer.dashboard import render_dashboard, DashboardData
from .renderer.bitmap import rgb_to_bitplanes
from .renderer.snapshot import DashboardSnapshot, DeviceStatus, ModelUsage
from .renderer.firmware_render import encode_with_crc
from .ble.transport import BLEDeviceNotFoundError, BLETransport

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    handlers=[logging.StreamHandler(sys.stdout)],
)
logger = logging.getLogger("dashboard")


@dataclass(frozen=True)
class CollectedSnapshot:
    """主机侧预采集结果，BLE 连接窗口内直接复用。"""
    usage: Any
    glm_plan: Any
    gpt_plan: Any
    cache_dict: dict
    cache_hash: str
    collected_at: datetime


class CollectionCache:
    """保存后台采集到的最新看板数据。"""

    def __init__(self):
        self._latest: CollectedSnapshot | None = None
        self._lock = asyncio.Lock()

    async def get(self) -> CollectedSnapshot | None:
        async with self._lock:
            return self._latest

    async def set(self, snapshot: CollectedSnapshot) -> None:
        async with self._lock:
            self._latest = snapshot


def _build_dashboard_snapshot(
    usage,
    glm_plan,
    gpt_plan,
    device_status: DeviceStatus | None,
    captured_at: datetime,
) -> DashboardSnapshot:
    """Build unified DashboardSnapshot from collected data."""
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
        generated_at=captured_at.isoformat(),
        last_refresh_label=captured_at.strftime("%H:%M"),
        total_tokens=usage.total_tokens,
        input_tokens=usage.input_tokens,
        output_tokens=usage.output_tokens,
        cache_tokens=usage.cache_read_tokens,
        glm_plan=glm_plan,
        gpt_plan=gpt_plan,
        models=models,
        device=device_status,
        sync_hour=captured_at.hour,
        sync_minute=captured_at.minute,
        sync_second=captured_at.second,
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


def _format_cache_summary(cache_dict: dict, cache_hash: str) -> str:
    """生成缓存判定日志摘要，便于判断是否真的有数据变化。"""
    return (
        f"hash={cache_hash[:8]}, total={cache_dict['total_tokens']}, "
        f"input={cache_dict['input_tokens']}, output={cache_dict['output_tokens']}, "
        f"cache={cache_dict['cache_tokens']}, models={len(cache_dict['models'])}"
    )


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
        cache_to_save = dict(cache_dict)
        cache_to_save["hash"] = cache_hash
        cache_to_save["timestamp"] = datetime.now().isoformat()
        with open(_CACHE_FILE, "w", encoding="utf-8") as f:
            json.dump(cache_to_save, f, ensure_ascii=False, indent=2)
    except Exception:
        pass


def _select_refresh_mode(device_status: DeviceStatus) -> str:
    logger.info(
        "MSD partial_baseline_ready=%d, force_full_refresh=%d",
        int(device_status.partial_baseline_ready),
        int(device_status.force_full_refresh),
    )
    if device_status.force_full_refresh or not device_status.partial_baseline_ready:
        return "FULL"
    return "FAST"


def _collect_snapshot() -> CollectedSnapshot | None:
    """收集本轮看板所需的数据。"""
    logger.info("Collecting data...")
    usage = collect_today_usage()
    glm_plan = collect_glm_plan()
    gpt_plan = collect_gpt_plan()
    if not glm_plan.available and not gpt_plan.available:
        logger.warning("Both plans unavailable, keeping previous collection cache")
        return None

    captured_at = datetime.now()
    snapshot = _build_dashboard_snapshot(usage, glm_plan, gpt_plan, None, captured_at)
    cache_dict = _snapshot_to_cache_dict(snapshot)
    cache_hash = _compute_cache_hash(cache_dict)
    logger.info(
        "Collection cache refreshed: "
        f"{_format_cache_summary(cache_dict, cache_hash)}"
    )
    return CollectedSnapshot(
        usage=usage,
        glm_plan=glm_plan,
        gpt_plan=gpt_plan,
        cache_dict=cache_dict,
        cache_hash=cache_hash,
        collected_at=captured_at,
    )


async def _refresh_collection_cache(collection_cache: CollectionCache) -> None:
    collected = await asyncio.to_thread(_collect_snapshot)
    if collected:
        await collection_cache.set(collected)


async def _collection_loop(collection_cache: CollectionCache) -> None:
    """每 30 秒后台采集一次，避免 BLE 窗口内等待接口返回。"""
    while True:
        await asyncio.sleep(COLLECT_INTERVAL_SECONDS)
        try:
            await _refresh_collection_cache(collection_cache)
        except Exception:
            logger.error("Collection refresh failed", exc_info=True)


async def _read_device_status_for_cycle(transport: BLETransport) -> DeviceStatus:
    """先占住 BLE 唤醒窗口，再读取设备状态。"""
    await transport.connect()
    device_status = await transport.read_device_status()
    logger.info(
        f"Device: WiFi={'connected' if device_status.wifi_connected else 'disconnected'}, "
        f"battery={device_status.battery_percent}%, "
        f"force_full={int(device_status.force_full_refresh)}"
    )
    return device_status


def _is_synced_to_device(collected: CollectedSnapshot) -> bool:
    last_cache = _load_last_cache()
    return bool(last_cache and last_cache.get("hash") == collected.cache_hash)


def _must_push(device_status: DeviceStatus, synced: bool) -> bool:
    if device_status.force_full_refresh:
        return True
    if TOKEN_DASHBOARD_RENDER_MODE == "firmware_render":
        return not synced or not device_status.partial_baseline_ready
    return not synced


async def _handle_no_change(transport: BLETransport, collected: CollectedSnapshot) -> None:
    logger.info(
        "Device found, data unchanged, sending sleep command: "
        f"{_format_cache_summary(collected.cache_dict, collected.cache_hash)}"
    )
    await transport.send_no_change_sleep()


async def run_cycle(transport: BLETransport, collection_cache: CollectionCache) -> bool:
    """连接设备后只使用预采集缓存完成本轮同步。"""
    collected = await collection_cache.get()
    if not collected:
        logger.warning("No collection cache available, skipping BLE cycle")
        return False

    device_status = await _read_device_status_for_cycle(transport)
    synced = _is_synced_to_device(collected)
    if not _must_push(device_status, synced):
        await _handle_no_change(transport, collected)
        return True

    refresh_mode = "FULL"
    if TOKEN_DASHBOARD_RENDER_MODE == "firmware_render":
        refresh_mode = _select_refresh_mode(device_status)
        logger.info(f"Selected refresh mode: {refresh_mode}")

    logger.info(
        "Data changed since last cycle, proceeding with update: "
        f"{_format_cache_summary(collected.cache_dict, collected.cache_hash)}"
    )

    # 真正发送前重新取一次时间，保证屏幕时间和 RTC 同步时间尽量贴近设备接收时刻。
    snapshot = _build_dashboard_snapshot(
        collected.usage,
        collected.glm_plan,
        collected.gpt_plan,
        device_status,
        datetime.now(),
    )

    if TOKEN_DASHBOARD_RENDER_MODE == "bitmap":
        success = await _run_bitmap_mode(transport, snapshot)
    elif TOKEN_DASHBOARD_RENDER_MODE == "firmware_render":
        success = await _run_firmware_render_mode(transport, snapshot, refresh_mode)
    else:
        logger.error(f"Unknown render mode: {TOKEN_DASHBOARD_RENDER_MODE}")
        return False

    if success:
        _save_cache(collected.cache_dict, collected.cache_hash)
    logger.info(f"Update {'succeeded' if success else 'failed'}")
    return success


def _seconds_until_next_device_window() -> float:
    now = datetime.now()
    elapsed = (
        now.minute * 60 + now.second + now.microsecond / 1_000_000
    ) % DEVICE_WAKE_INTERVAL_SECONDS
    wait_seconds = DEVICE_WAKE_INTERVAL_SECONDS - elapsed
    if wait_seconds <= POST_DEVICE_HANDLED_GUARD_SECONDS:
        wait_seconds += DEVICE_WAKE_INTERVAL_SECONDS
    return max(1.0, wait_seconds - POST_DEVICE_HANDLED_GUARD_SECONDS)


async def _sleep_after_device_handled() -> None:
    wait_seconds = _seconds_until_next_device_window()
    logger.info(f"Device handled, pausing scan for {wait_seconds:.1f}s")
    await asyncio.sleep(wait_seconds)


async def main_loop() -> None:
    """常驻扫描设备广播，发现唤醒窗口后执行一次推送。"""
    logger.info("Token 用量看板 started")
    logger.info(f"Waiting for device broadcasts, scan timeout={BLE_SCAN_TIMEOUT_SECONDS:.1f}s")

    transport = BLETransport()
    collection_cache = CollectionCache()
    try:
        await _refresh_collection_cache(collection_cache)
    except Exception:
        logger.error("Initial collection failed", exc_info=True)
    collection_task = asyncio.create_task(_collection_loop(collection_cache))

    try:
        while True:
            device_handled = False
            try:
                device_handled = await run_cycle(transport, collection_cache)
            except BLEDeviceNotFoundError:
                logger.info(
                    f"No device broadcast found in this {BLE_SCAN_TIMEOUT_SECONDS:.1f}s scan window"
                )
            except Exception:
                logger.error("Cycle failed", exc_info=True)
                await transport.disconnect()
                # 连接失败后重建 transport，释放累积的 WinRT BLE 资源
                transport = BLETransport()
            else:
                if transport.client and transport.client.is_connected:
                    await transport.disconnect()
            if device_handled:
                await _sleep_after_device_handled()
    finally:
        collection_task.cancel()
        try:
            await collection_task
        except asyncio.CancelledError:
            pass


def main():
    """Entry point."""
    try:
        asyncio.run(main_loop())
    except KeyboardInterrupt:
        logger.info("Stopped by user")


if __name__ == "__main__":
    main()
