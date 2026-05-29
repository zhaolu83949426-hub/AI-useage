# COM4 设备硬件探测记录

## 已确认信息

- 串口：`COM4`
- USB 转串口：`USB-SERIAL CH340`
- 主控：`ESP32-D0WD-V3 (revision v3.1)`
- 无线能力：`Wi-Fi + BT`
- 晶振：`40MHz`
- Flash：`4MB`
- Flash ID：`Manufacturer 0x5E / Device 0x4016`
- Flash 供电：`3.3V`
- MAC：`F0:24:F9:0C:2E:24`

以上信息来自 `esptool` 与 Windows 设备枚举探测。

## 当前配置

当前设备运行 **J-Calendar z21** 固件（`jcalendar-1.1.9/` 目录，`z21` 编译环境），叠加 Token 用量看板功能。

硬件接线：

| 信号 | GPIO |
|------|------|
| `BUSY` | `4` |
| `CS` | `5` |
| `RST` | `16` |
| `DC` | `17` |
| `SCK` | `18` |
| `MOSI` | `23` |
| 电池 ADC | `32` |
| 按钮 | `14` (另一端 GND) |

## 屏幕型号确认

当前使用的 4.2 寸黑白红三色墨水屏，按 `jcalendar-1.1.9` 固件的驱动映射：

- `SI_DRIVER=21` 对应 `z21` 环境
- 适用于 SES 拆机屏，丝印为 `A13600**`

如需更换屏幕，参考 [jcalendar-1.1.9/README.md](../jcalendar-1.1.9/README.md) Q&A 第 4 项的丝印对照表选择对应固件环境。

## 烧录参考

```bash
cd jcalendar-1.1.9
pio run -e z21

PYTHONIOENCODING=utf-8 "$HOME/.platformio/penv/Scripts/esptool.exe" \
  --chip esp32 --port COM4 --baud 115200 \
  --before default-reset --after hard-reset \
  write-flash -z --flash-mode dio --flash-freq 40m --flash-size detect \
  0x1000 .pio/build/z21/bootloader.bin \
  0x8000 .pio/build/z21/partitions.bin \
  0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 .pio/build/z21/firmware.bin \
  2>&1 | cat
```
