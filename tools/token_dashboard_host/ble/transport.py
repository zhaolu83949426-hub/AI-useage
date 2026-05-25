"""BLE transport for ESP32 e-paper display communication."""

import asyncio
import logging
import struct

import bleak

from ..config import (
    BLE_SERVICE_UUID, BLE_CHAR_UUID, BLE_DEVICE_NAME_PREFIX,
    BLE_CHUNK_SIZE, PLANE_SIZE,
    BATTERY_VOLTAGE_MAX, BATTERY_VOLTAGE_MIN,
)
from ..renderer.dashboard import DeviceStatus

logger = logging.getLogger(__name__)

# Command opcodes (big-endian)
CMD_READ_MSD = bytes([0x00, 0x44])
CMD_DIRECT_WRITE_START = bytes([0x00, 0x70])
CMD_DIRECT_WRITE_DATA = bytes([0x00, 0x71])
CMD_DIRECT_WRITE_END = bytes([0x00, 0x72])


class BLETransportError(Exception):
    pass


class BLETransport:
    """Manages BLE connection and image upload to ESP32 e-paper display."""

    def __init__(self):
        self.client: bleak.BleakClient | None = None
        self._response_event = asyncio.Event()
        self._response_data = bytearray()

    async def connect(self) -> None:
        """Scan for and connect to the ESP32 device."""
        device = await bleak.BleakScanner.find_device_by_filter(
            lambda d, _: d.name and d.name.startswith(BLE_DEVICE_NAME_PREFIX),
            timeout=30.0,
        )
        if not device:
            raise BLETransportError(f"Device not found (prefix={BLE_DEVICE_NAME_PREFIX})")

        self.client = bleak.BleakClient(device, timeout=15.0)
        await self.client.connect()
        await self.client.start_notify(BLE_CHAR_UUID, self._on_notification)
        logger.info(f"Connected to {device.name}")

    async def disconnect(self) -> None:
        """Disconnect from the device."""
        if self.client and self.client.is_connected:
            try:
                await self.client.stop_notify(BLE_CHAR_UUID)
            except Exception:
                pass
            await self.client.disconnect()
            logger.info("Disconnected")

    async def read_device_status(self) -> DeviceStatus:
        """Read battery voltage and WiFi status via READ_MSD command."""
        resp = await self._send_command(CMD_READ_MSD, timeout=10.0)

        if len(resp) < 4 or resp[0] != 0x00:
            logger.warning(f"READ_MSD unexpected response: {resp.hex()}")
            return DeviceStatus()

        # Parse MSD payload (starting at offset 2)
        # msd_payload bytes 14-15 encode battery voltage:
        #   byte 14: voltage in deca-mV (low byte)
        #   byte 15 bit 0: voltage high bit
        # So voltage = ((byte15 & 0x01) << 8 | byte14) * 10 mV
        if len(resp) >= 18:
            msd = resp[2:18]
            voltage_raw = ((msd[15] & 0x01) << 8) | msd[14]
            voltage_mv = voltage_raw * 10
            battery_pct = _voltage_to_percent(voltage_mv / 1000.0)
        else:
            battery_pct = 0

        # Check for wifi_connected byte (firmware extension)
        wifi_connected = False
        if len(resp) >= 19:
            wifi_connected = resp[18] == 0x01

        return DeviceStatus(
            wifi_connected=wifi_connected,
            battery_percent=battery_pct,
            available=True,
        )

    async def send_3color_image(self, black_plane: bytes, red_plane: bytes) -> bool:
        """Send a 3-color image to the display.

        Protocol flow (requires firmware dual-plane patch):
          START -> DATA(15K black) -> END(transition) -> DATA(15K red) -> END(refresh)

        Returns True if refresh succeeded.
        """
        assert len(black_plane) == PLANE_SIZE
        assert len(red_plane) == PLANE_SIZE

        # Phase 1: DIRECT_WRITE_START
        resp = await self._send_command(CMD_DIRECT_WRITE_START)
        if resp[0] != 0x00:
            raise BLETransportError(f"DIRECT_WRITE_START failed: {resp.hex()}")

        # Phase 2: Send PLANE_0 (black/white) data
        await self._send_chunks(black_plane)

        # Phase 3: First DIRECT_WRITE_END (firmware transitions to PLANE_1)
        resp = await self._send_command(CMD_DIRECT_WRITE_END + bytes([0x00]))
        if resp[0] != 0x00:
            raise BLETransportError(f"First DIRECT_WRITE_END failed: {resp.hex()}")

        # Phase 4: Send PLANE_1 (red) data
        await self._send_chunks(red_plane)

        # Phase 5: Second DIRECT_WRITE_END (triggers display refresh)
        resp = await self._send_command(CMD_DIRECT_WRITE_END + bytes([0x00]))
        if resp[0] != 0x00:
            raise BLETransportError(f"Second DIRECT_WRITE_END failed: {resp.hex()}")

        # Phase 6: Wait for refresh completion
        refresh_resp = await self._wait_response(timeout=65.0)
        if refresh_resp and len(refresh_resp) >= 2:
            if refresh_resp[1] == 0x73:
                logger.info("Display refresh succeeded")
                return True
            elif refresh_resp[1] == 0x74:
                logger.warning("Display refresh timeout")
                return False

        return False

    async def _send_chunks(self, data: bytes) -> None:
        """Send data in BLE-sized chunks, waiting for ACK after each."""
        offset = 0
        total = len(data)
        while offset < total:
            chunk = data[offset:offset + BLE_CHUNK_SIZE]
            payload = CMD_DIRECT_WRITE_DATA + chunk
            resp = await self._send_command(payload, timeout=10.0)
            if resp[0] != 0x00:
                raise BLETransportError(f"DATA ACK failed at offset {offset}/{total}")
            offset += len(chunk)

    async def _send_command(self, payload: bytes, timeout: float = 10.0) -> bytes:
        """Send a BLE command and wait for the response notification."""
        if not self.client or not self.client.is_connected:
            raise BLETransportError("Not connected")

        self._response_data.clear()
        self._response_event.clear()

        await self.client.write_gatt_char(BLE_CHAR_UUID, payload, response=False)

        resp = await self._wait_response(timeout=timeout)
        if resp is None:
            raise BLETransportError(f"Command timeout: {payload[:2].hex()}")
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


def _voltage_to_percent(voltage: float) -> int:
    """Convert battery voltage to percentage (0-100)."""
    if voltage >= BATTERY_VOLTAGE_MAX:
        return 100
    if voltage <= BATTERY_VOLTAGE_MIN:
        return 0
    return int((voltage - BATTERY_VOLTAGE_MIN) / (BATTERY_VOLTAGE_MAX - BATTERY_VOLTAGE_MIN) * 100)
