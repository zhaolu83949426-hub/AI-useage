#include <Arduino.h>
#include <SPI.h>

#include "app_config.h"
#include "app_state.h"
#include "ble_service.h"
#include "display_service.h"
#include "protocol.h"

void setup() {
    Serial.begin(115200);
    delay(100);
    SPI.begin(SPI_SCK, -1, SPI_MOSI, SPI_CS);
    init_protocol_state();
    init_ble_service();
    init_display_service();
}

static void process_one_command() {
    PacketQueue<app::kCommandQueueSize, app::kMaxPacketSize>::Item item;
    if (!g_commandQueue.pop(item)) {
        return;
    }
    process_command_packet(item.data, item.len);
}

void loop() {
    process_one_command();
    process_ble_responses();
    process_deferred_job();
    process_ble_responses();
    process_ble_advertising_restart();
    delay(1);
}
