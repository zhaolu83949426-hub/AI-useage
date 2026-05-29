#include "ble_service.h"

#include <Arduino.h>

#include <NimBLEDevice.h>

#include "app_config.h"
#include "app_state.h"
#include "protocol.h"

namespace {

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
        (void)server;
        (void)info;
        g_bleConnected = true;
        g_rebootFlag = false;
        refresh_msd_payload();
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override {
        (void)server;
        (void)info;
        (void)reason;
        g_bleConnected = false;
        g_advertisingRestartPending = true;
    }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
        (void)info;
        std::string value = characteristic->getValue();
        if (value.empty() || value.size() > app::kMaxPacketSize) {
            return;
        }
        g_commandQueue.push(reinterpret_cast<const uint8_t*>(value.data()), value.size());
    }
};

ServerCallbacks g_serverCallbacks;
CharacteristicCallbacks g_characteristicCallbacks;

void start_advertising() {
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    if (!advertising) {
        return;
    }

    NimBLEAdvertisementData adv_data;
    String device_name = "OD" + get_chip_id_hex();
    adv_data.setName(device_name.c_str());
    adv_data.setFlags(0x06);
    advertising->setAdvertisementData(adv_data);
    advertising->addServiceUUID(app::kBleServiceUuid);
    advertising->enableScanResponse(false);
    advertising->start();
}

}  // namespace

void init_ble_service() {
    String device_name = "OD" + get_chip_id_hex();
    NimBLEDevice::init(device_name.c_str());
    NimBLEDevice::setMTU(app::kBleMtu);
    g_bleServer = NimBLEDevice::createServer();
    g_bleServer->setCallbacks(&g_serverCallbacks);
    g_bleService = g_bleServer->createService(app::kBleServiceUuid);
    g_bleCharacteristic = g_bleService->createCharacteristic(
        app::kBleCharacteristicUuid,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY |
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    g_bleCharacteristic->setCallbacks(&g_characteristicCallbacks);
    start_advertising();
}

void process_ble_responses() {
    if (!g_bleConnected || !g_bleCharacteristic) {
        return;
    }

    PacketQueue<app::kResponseQueueSize, app::kMaxPacketSize>::Item item;
    while (g_responseQueue.pop(item)) {
        g_bleCharacteristic->setValue(item.data, item.len);
        g_bleCharacteristic->notify();
        delay(10);
    }
}

void process_ble_advertising_restart() {
    if (!g_advertisingRestartPending || g_bleConnected) {
        return;
    }
    g_advertisingRestartPending = false;
    start_advertising();
}
