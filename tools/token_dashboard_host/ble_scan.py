import asyncio
from bleak import BleakScanner

async def scan():
    print("Scanning for BLE devices...")
    devices = await BleakScanner.discover()
    print(f'Found {len(devices)} devices')
    for d in devices:
        if d.name:
            print(f'{d.name} - {d.address}')

asyncio.run(scan())
