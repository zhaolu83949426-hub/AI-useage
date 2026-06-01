#!/usr/bin/env python3
"""Z21 BW Fast Refresh Verification Script

This script verifies that the BW fast refresh mode is working correctly
by sending dashboard snapshots via BLE with both FULL and FAST refresh modes
and measuring the time difference.
"""

import asyncio
import struct
import time
import logging
from typing import NamedTuple

import bleak

# Configuration
BLE_SERVICE_UUID = "00002446-0000-1000-8000-00805f9b34fb"
BLE_CHAR_UUID = "00002446-0000-1000-8000-00805f9b34fb"
BLE_DEVICE_NAME_PREFIX = "OD"
BLE_CHUNK_SIZE = 500

# Dashboard render commands
CMD_DASHBOARD_RENDER_START = bytes([0x00, 0x78])
CMD_DASHBOARD_RENDER_DATA = bytes([0x00, 0x79])
CMD_DASHBOARD_RENDER_COMMIT = bytes([0x00, 0x7A])

# Logging setup
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class RefreshResult(NamedTuple):
    """Result of a refresh test."""
    mode: str
    success: bool
    total_time: float
    refresh_time: float


class BWRefreshVerifier:
    """Verifier for BW fast refresh functionality."""

    def __init__(self):
        self.client: bleak.BleakClient | None = None
        self._response_event = asyncio.Event()
        self._response_data = bytearray()

    async def connect(self) -> None:
        """Scan for and connect to the ESP32 device."""
        logger.info("Scanning for device...")
        device = await bleak.BleakScanner.find_device_by_filter(
            lambda d, _: d.name and d.name.startswith(BLE_DEVICE_NAME_PREFIX),
            timeout=30.0,
        )
        if not device:
            raise RuntimeError(f"Device not found (prefix={BLE_DEVICE_NAME_PREFIX})")

        logger.info(f"Found device: {device.name} ({device.address})")
        self.client = bleak.BleakClient(device, timeout=15.0)
        await self.client.connect()
        await self.client.start_notify(BLE_CHAR_UUID, self._on_notification)
        logger.info("Connected successfully")

    async def disconnect(self) -> None:
        """Disconnect from the device."""
        if self.client and self.client.is_connected:
            try:
                await self.client.stop_notify(BLE_CHAR_UUID)
            except Exception:
                pass
            await self.client.disconnect()
            logger.info("Disconnected")

    async def test_refresh(self, refresh_mode: str) -> RefreshResult:
        """Test a refresh mode and measure timing.

        Args:
            refresh_mode: "FULL" or "FAST"

        Returns:
            RefreshResult with timing and success status
        """
        logger.info(f"\n{'='*60}")
        logger.info(f"Testing {refresh_mode} refresh mode")
        logger.info(f"{'='*60}")

        # Create test dashboard snapshot payload (192 bytes)
        payload = self._create_test_payload()
        crc32 = self._calculate_crc32(payload)

        # Map refresh mode
        refresh_byte = 0 if refresh_mode == "FULL" else 1

        total_start = time.time()

        try:
            # START command
            logger.info("Sending START command...")
            start_payload = CMD_DASHBOARD_RENDER_START + struct.pack(
                "<BBHI",
                1,  # version
                0,  # flags
                len(payload),  # payload_len
                crc32,  # crc32
            )

            resp = await self._send_command(start_payload, timeout=10.0)
            if resp[0] != 0x00 or resp[1] != 0x78:
                raise RuntimeError(f"START failed: {resp.hex()}")
            logger.info("✓ START acknowledged")

            # Send DATA chunks
            logger.info("Sending DATA chunks...")
            offset = 0
            chunk_count = 0
            while offset < len(payload):
                chunk = payload[offset:offset + BLE_CHUNK_SIZE]
                resp = await self._send_command(
                    CMD_DASHBOARD_RENDER_DATA + chunk, timeout=10.0
                )
                if resp[0] != 0x00 or resp[1] != 0x79:
                    raise RuntimeError(f"DATA failed at offset {offset}")
                offset += len(chunk)
                chunk_count += 1
            logger.info(f"✓ Sent {chunk_count} data chunks ({len(payload)} bytes)")

            # COMMIT command
            logger.info(f"Sending COMMIT command (mode={refresh_mode})...")
            commit_payload = CMD_DASHBOARD_RENDER_COMMIT + bytes([refresh_byte])

            # Send COMMIT and wait for ACK first
            refresh_start = time.time()
            resp = await self._send_command(commit_payload, timeout=120.0)

            if resp[0] != 0x00 or resp[1] != 0x7A:
                raise RuntimeError(f"COMMIT failed: {resp.hex()}")
            logger.info("✓ COMMIT acknowledged")

            # Clear state before waiting for refresh result
            self._response_data.clear()
            self._response_event.clear()

            # Wait for refresh completion
            logger.info("Waiting for display refresh to complete...")
            refresh_resp = await self._wait_response(timeout=65.0)
            refresh_time = time.time() - refresh_start

            if refresh_resp and len(refresh_resp) >= 2:
                if refresh_resp[1] == 0x7B:
                    logger.info(f"✓ Refresh succeeded ({refresh_time:.1f}s)")
                    total_time = time.time() - total_start
                    return RefreshResult(
                        mode=refresh_mode,
                        success=True,
                        total_time=total_time,
                        refresh_time=refresh_time
                    )
                elif refresh_resp[1] == 0x7C:
                    logger.warning(f"✗ Refresh timeout ({refresh_time:.1f}s)")
                    return RefreshResult(
                        mode=refresh_mode,
                        success=False,
                        total_time=time.time() - total_start,
                        refresh_time=refresh_time
                    )

            logger.warning(f"✗ Unexpected response: {refresh_resp.hex() if refresh_resp else 'None'}")
            return RefreshResult(
                mode=refresh_mode,
                success=False,
                total_time=time.time() - total_start,
                refresh_time=refresh_time
            )

        except Exception as e:
            logger.error(f"✗ Test failed: {e}")
            return RefreshResult(
                mode=refresh_mode,
                success=False,
                total_time=time.time() - total_start,
                refresh_time=0.0
            )

    async def run_verification(self) -> None:
        """Run complete verification sequence."""
        logger.info("\n" + "="*60)
        logger.info("Z21 BW Fast Refresh Verification")
        logger.info("="*60)

        await self.connect()

        try:
            # Establish baseline with FULL first, then test FAST against it.
            full_result = await self.test_refresh("FULL")
            await asyncio.sleep(2)
            fast_result = await self.test_refresh("FAST")

            # Print comparison results
            self._print_comparison(fast_result, full_result)

        finally:
            await self.disconnect()

    def _create_test_payload(self) -> bytes:
        """Create a test dashboard snapshot payload (192 bytes)."""
        # Create a simple test pattern
        payload = bytearray(192)

        # Fill with recognizable pattern
        for i in range(192):
            payload[i] = i & 0xFF

        # Second run changes only summary/model values so FAST can use existing baseline.
        if not hasattr(self, "_payload_counter"):
            self._payload_counter = 0
        self._payload_counter += 1
        payload[0:4] = struct.pack("<I", 12345678 + self._payload_counter * 1000)  # total_tokens
        payload[4:8] = struct.pack("<I", 10000000)  # input_tokens
        payload[8:12] = struct.pack("<I", 2000000)  # output_tokens
        payload[12:16] = struct.pack("<I", 350000)  # cache_tokens

        return bytes(payload)

    def _calculate_crc32(self, data: bytes) -> int:
        """Calculate CRC32 checksum."""
        import zlib
        return zlib.crc32(data) & 0xFFFFFFFF

    async def _send_command(self, payload: bytes, timeout: float = 10.0) -> bytes:
        """Send a BLE command and wait for the response notification."""
        if not self.client or not self.client.is_connected:
            raise RuntimeError("Not connected")

        self._response_data.clear()
        self._response_event.clear()

        await self.client.write_gatt_char(BLE_CHAR_UUID, payload, response=False)

        resp = await self._wait_response(timeout=timeout)
        if resp is None:
            raise RuntimeError(f"Command timeout: {payload[:2].hex()}")
        return resp

    async def _wait_response(self, timeout: float = 10.0) -> bytes | None:
        """Wait for a notification response."""
        try:
            await asyncio.wait_for(self._response_event.wait(), timeout=timeout)
            return bytes(self._response_data)
        except asyncio.TimeoutError:
            return None

    def _on_notification(self, sender, data: bytearray) -> None:
        """Handle BLE notification (response from device)."""
        self._response_data = data
        self._response_event.set()

    def _print_comparison(self, fast: RefreshResult, full: RefreshResult) -> None:
        """Print comparison results."""
        logger.info("\n" + "="*60)
        logger.info("VERIFICATION RESULTS")
        logger.info("="*60)

        logger.info(f"\nFAST Refresh:")
        logger.info(f"  Status: {'✓ PASS' if fast.success else '✗ FAIL'}")
        logger.info(f"  Total time: {fast.total_time:.1f}s")
        logger.info(f"  Refresh time: {fast.refresh_time:.1f}s")

        logger.info(f"\nFULL Refresh:")
        logger.info(f"  Status: {'✓ PASS' if full.success else '✗ FAIL'}")
        logger.info(f"  Total time: {full.total_time:.1f}s")
        logger.info(f"  Refresh time: {full.refresh_time:.1f}s")

        if fast.success and full.success:
            speedup = full.refresh_time / fast.refresh_time if fast.refresh_time > 0 else 0
            time_saved = full.refresh_time - fast.refresh_time

            logger.info(f"\nComparison:")
            logger.info(f"  Time saved: {time_saved:.1f}s")
            logger.info(f"  Speedup: {speedup:.1f}x")

            if speedup >= 2.0:
                logger.info(f"  ✓ BW fast refresh is working well ({speedup:.1f}x faster)")
            elif speedup >= 1.3:
                logger.info(f"  ✓ BW fast refresh is working ({speedup:.1f}x faster)")
            else:
                logger.warning(f"  ⚠ Fast refresh may not be significantly faster")

        logger.info("\n" + "="*60)


async def main():
    """Main entry point."""
    verifier = BWRefreshVerifier()
    try:
        await verifier.run_verification()
    except KeyboardInterrupt:
        logger.info("\nVerification interrupted by user")
    except Exception as e:
        logger.error(f"\nVerification failed: {e}", exc_info=True)


if __name__ == "__main__":
    asyncio.run(main())
