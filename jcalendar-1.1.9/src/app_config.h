#pragma once

#include <stddef.h>
#include <stdint.h>

#include "wiring.h"

namespace app {

constexpr char kBleServiceUuid[] = "00002446-0000-1000-8000-00805F9B34FB";
constexpr char kBleCharacteristicUuid[] = "00002446-0000-1000-8000-00805F9B34FB";
constexpr uint16_t kBleMtu = 512;

constexpr uint16_t kDisplayWidth = 400;
constexpr uint16_t kDisplayHeight = 300;
constexpr uint16_t kDisplayPageHeight = 32;
constexpr size_t kPlaneSize = (kDisplayWidth * kDisplayHeight) / 8;

constexpr uint8_t kPinBusy = SPI_BUSY;
constexpr uint8_t kPinCs = SPI_CS;
constexpr uint8_t kPinReset = SPI_RST;
constexpr uint8_t kPinDc = SPI_DC;
constexpr uint8_t kPinClk = SPI_SCK;
constexpr uint8_t kPinData = SPI_MOSI;
constexpr uint8_t kBatterySensePin = PIN_ADC;
constexpr uint8_t kBatterySenseEnablePin = 0xFF;
constexpr uint16_t kBatteryVoltageScalingFactor = 161;

constexpr size_t kMaxPacketSize = 512;
constexpr size_t kCommandQueueSize = 8;
constexpr size_t kResponseQueueSize = 16;

constexpr uint8_t kCidLow = 0x46;
constexpr uint8_t kCidHigh = 0x24;
constexpr uint8_t kFirmwareMajor = 0;
constexpr uint8_t kFirmwareMinor = 3;

constexpr uint8_t kRefreshModeFull = 0;
constexpr uint8_t kRefreshModeFast = 1;
constexpr uint8_t kMsdFlagPartialBaselineReady = 1 << 3;
constexpr uint8_t kFastRefreshMaintenanceInterval = 12;
constexpr uint8_t kFastRefreshBatteryThresholdPercent = 2;
constexpr uint8_t kMaxFastRefreshRects = 4;
constexpr uint32_t kMaxFastRefreshArea = kDisplayWidth * 120;

constexpr uint32_t kDeferredRenderDelayMs = 80;

}  // namespace app
