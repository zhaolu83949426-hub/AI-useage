"""Token 用量看板 - Main entry point with 5-minute refresh scheduler."""

import asyncio
import logging
import sys
from datetime import datetime

from .config import REFRESH_INTERVAL_SECONDS
from .collectors.aiusage import collect_today_usage
from .collectors.glm_plan import collect_glm_plan
from .collectors.gpt_plan import collect_gpt_plan
from .renderer.dashboard import render_dashboard, DashboardData
from .renderer.bitmap import rgb_to_bitplanes
from .ble.transport import BLETransport

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    handlers=[logging.StreamHandler(sys.stdout)],
)
logger = logging.getLogger("dashboard")


async def run_cycle(transport: BLETransport) -> None:
    """Execute one complete collect-render-send cycle."""
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
    logger.info("Connecting to device...")
    await transport.connect()
    device_status = await transport.read_device_status()
    logger.info(f"Device: WiFi={'connected' if device_status.wifi_connected else 'disconnected'}, "
                f"battery={device_status.battery_percent}%")

    # 4. Render dashboard
    now = datetime.now()
    last_refresh = now.strftime("%H:%M")

    data = DashboardData(
        usage=usage,
        glm_plan=glm_plan,
        gpt_plan=gpt_plan,
        device=device_status,
        last_refresh=last_refresh,
    )
    img = render_dashboard(data)

    # Save debug PNG
    debug_path = "dashboard_debug.png"
    img.save(debug_path)
    logger.info(f"Debug image saved to {debug_path}")

    # 5. Convert to bitplanes
    black_plane, red_plane = rgb_to_bitplanes(img)

    # 6. Send via BLE
    logger.info("Sending image to display...")
    success = await transport.send_3color_image(black_plane, red_plane)
    logger.info(f"Update {'succeeded' if success else 'failed'}")


async def main_loop() -> None:
    """Main event loop with 5-minute refresh interval."""
    logger.info("Token 用量看板 started")
    logger.info(f"Refresh interval: {REFRESH_INTERVAL_SECONDS}s")

    transport = BLETransport()

    # Run first cycle immediately
    try:
        await run_cycle(transport)
    except Exception:
        logger.error("First cycle failed", exc_info=True)
    finally:
        if transport.client and transport.client.is_connected:
            await transport.disconnect()

    # Subsequent cycles on interval
    while True:
        await asyncio.sleep(REFRESH_INTERVAL_SECONDS)
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
