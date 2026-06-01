#include "protocol.h"

#include <Arduino.h>
#include <esp_system.h>

#include "app_config.h"
#include "app_state.h"
#include "dashboard_protocol.h"
#include "dashboard_renderer.h"
#include "display_service.h"

namespace {

void queue_bytes(const uint8_t* data, uint16_t len) {
    if (!data || len == 0) {
        return;
    }
    g_responseQueue.push(data, len);
}

void queue_simple_response(uint8_t b0, uint8_t b1) {
    uint8_t response[] = {b0, b1};
    queue_bytes(response, sizeof(response));
}

void queue_error_response(uint8_t opcode, uint8_t error) {
    uint8_t response[] = {0xFF, opcode, error, 0x00};
    queue_bytes(response, sizeof(response));
}

void reset_direct_write_state() {
    g_directWriteState.active = false;
    g_directWriteState.phase = DirectWritePhase::Idle;
    g_directWriteState.received_bytes = 0;
}

uint16_t read_u16_le(const uint8_t* data) {
    return data[0] | (data[1] << 8);
}

float read_battery_voltage() {
    pinMode(app::kBatterySensePin, INPUT);
    if (app::kBatterySenseEnablePin != 0xFF) {
        pinMode(app::kBatterySenseEnablePin, OUTPUT);
        digitalWrite(app::kBatterySenseEnablePin, HIGH);
    }

    uint32_t adc_sum = 0;
    for (int i = 0; i < 10; i++) {
        adc_sum += analogRead(app::kBatterySensePin);
        delay(2);
    }

    if (app::kBatterySenseEnablePin != 0xFF) {
        digitalWrite(app::kBatterySenseEnablePin, LOW);
    }
    return (adc_sum / 10.0f) * app::kBatteryVoltageScalingFactor / 100000.0f;
}

uint8_t encode_temperature() {
    float chip_temperature = temperatureRead();
    int16_t encoded = static_cast<int16_t>((chip_temperature + 40.0f) * 2.0f);
    if (encoded < 0) {
        encoded = 0;
    }
    if (encoded > 255) {
        encoded = 255;
    }
    return static_cast<uint8_t>(encoded);
}

void schedule_job(DeferredJob job) {
    // 刷屏延后到响应队列清空后执行，避免蓝牙传输和墨水屏刷新互相阻塞。
    g_deferredJob = job;
    g_deferredJobReadyAtMs = millis() + app::kDeferredRenderDelayMs;
}

void handle_firmware_version() {
    static const char kSha[] = "0000000000000000000000000000000000000000";
    uint8_t response[45] = {0};
    response[0] = 0x00;
    response[1] = 0x43;
    response[2] = app::kFirmwareMajor;
    response[3] = app::kFirmwareMinor;
    response[4] = 40;
    memcpy(response + 5, kSha, 40);
    queue_bytes(response, 45);
}

void handle_read_msd() {
    refresh_msd_payload();
    uint8_t response[19] = {0x00, 0x44};
    memcpy(response + 2, g_msdPayload, sizeof(g_msdPayload));
    response[18] = 0;
    queue_bytes(response, sizeof(response));
}

void handle_direct_write_start() {
    reset_direct_write_state();
    g_directWriteState.active = true;
    g_directWriteState.phase = DirectWritePhase::BlackPlane;
    queue_simple_response(0x00, 0x70);
}

bool append_plane_data(uint8_t* plane, size_t& received, const uint8_t* data, uint16_t len) {
    if (!plane || !data || len == 0) {
        return false;
    }

    size_t remaining = app::kPlaneSize - received;
    size_t bytes_to_copy = len > remaining ? remaining : len;
    memcpy(plane + received, data, bytes_to_copy);
    received += bytes_to_copy;
    return received == app::kPlaneSize;
}

void handle_direct_write_data(const uint8_t* data, uint16_t len) {
    if (!g_directWriteState.active || len == 0) {
        return;
    }
    if (g_directWriteState.phase == DirectWritePhase::BlackPlane) {
        bool complete = append_plane_data(g_directWriteState.black_plane, g_directWriteState.received_bytes, data, len);
        if (!complete) {
            queue_simple_response(0x00, 0x71);
            return;
        }
        g_directWriteState.phase = DirectWritePhase::RedPlane;
        g_directWriteState.received_bytes = 0;
        queue_simple_response(0x00, 0x72);
        return;
    }

    bool complete = append_plane_data(g_directWriteState.red_plane, g_directWriteState.received_bytes, data, len);
    if (!complete) {
        queue_simple_response(0x00, 0x71);
        return;
    }
    queue_simple_response(0x00, 0x72);
    schedule_job(DeferredJob::DirectWriteRefresh);
}

void handle_direct_write_end() {
    if (!g_directWriteState.active || g_directWriteState.phase != DirectWritePhase::RedPlane) {
        return;
    }
    if (g_directWriteState.received_bytes != app::kPlaneSize) {
        queue_error_response(0x72, 0x02);
        return;
    }
    queue_simple_response(0x00, 0x72);
    schedule_job(DeferredJob::DirectWriteRefresh);
}

void handle_dashboard_start(const uint8_t* payload, uint16_t len) {
    uint8_t response[8] = {};
    uint16_t response_len = dashboard_handle_start(g_dashboardContext, payload, len, response);
    queue_bytes(response, response_len);
}

void handle_dashboard_data(const uint8_t* payload, uint16_t len) {
    uint8_t response[8] = {};
    uint16_t response_len = dashboard_handle_data(g_dashboardContext, payload, len, response);
    queue_bytes(response, response_len);
}

void handle_dashboard_commit(uint8_t refresh_mode) {
    // 结构化看板走这里，校验通过后只排队一次整屏刷新。
    uint8_t response[8] = {};
    bool should_render = false;
    uint16_t response_len = dashboard_handle_commit(
        g_dashboardContext,
        refresh_mode,
        response,
        should_render
    );
    queue_bytes(response, response_len);
    if (should_render) {
        g_requestedRefreshMode = refresh_mode;
        schedule_job(DeferredJob::DashboardRender);
    }
}

}  // namespace

void init_protocol_state() {
    dashboard_protocol_init(g_dashboardContext);
    refresh_msd_payload();
}

String get_chip_id_hex() {
    uint64_t mac_address = ESP.getEfuseMac();
    uint32_t chip_id = static_cast<uint32_t>((mac_address >> 24) & 0xFFFFFF);
    char buffer[7] = {0};
    snprintf(buffer, sizeof(buffer), "%06X", chip_id);
    return String(buffer);
}

void refresh_msd_payload() {
    uint16_t cid = read_u16_le(reinterpret_cast<const uint8_t*>("\x46\x24"));
    float battery_voltage = read_battery_voltage();
    uint16_t voltage_10mv = static_cast<uint16_t>((battery_voltage * 1000.0f) / 10.0f);
    if (voltage_10mv > 511) {
        voltage_10mv = 511;
    }

    memset(g_msdPayload, 0, sizeof(g_msdPayload));
    g_msdPayload[0] = app::kCidLow;
    g_msdPayload[1] = app::kCidHigh;
    (void)cid;
    g_msdPayload[13] = encode_temperature();
    g_msdPayload[14] = static_cast<uint8_t>(voltage_10mv & 0xFF);
    g_msdPayload[15] = static_cast<uint8_t>(((voltage_10mv >> 8) & 0x01) |
        ((g_rebootFlag ? 1 : 0) << 1) |
        ((g_connectionRequested ? 1 : 0) << 2) |
        (g_dashboardRenderState.partial_baseline_ready ? app::kMsdFlagPartialBaselineReady : 0) |
        ((g_mloopCounter & 0x0F) << 4));
    g_mloopCounter = static_cast<uint8_t>((g_mloopCounter + 1) & 0x0F);
}

void process_command_packet(const uint8_t* data, uint16_t len) {
    if (!data || len < 2) {
        return;
    }

    uint16_t command = (data[0] << 8) | data[1];
    switch (command) {
        case 0x000F: ESP.restart(); return;
        case 0x0043: handle_firmware_version(); return;
        case 0x0044: handle_read_msd(); return;
        case 0x0070: handle_direct_write_start(); return;
        case 0x0071: handle_direct_write_data(data + 2, len - 2); return;
        case 0x0072: handle_direct_write_end(); return;
        case CMD_DASHBOARD_RENDER_START: handle_dashboard_start(data + 2, len - 2); return;
        case CMD_DASHBOARD_RENDER_DATA: handle_dashboard_data(data + 2, len - 2); return;
        case CMD_DASHBOARD_RENDER_COMMIT:
            handle_dashboard_commit(len > 2 ? data[2] : app::kRefreshModeFull);
            return;
        default: queue_error_response(static_cast<uint8_t>(command & 0xFF), 0x00); return;
    }
}

bool has_pending_responses() {
    return !g_responseQueue.empty();
}

void process_deferred_job() {
    if (g_deferredJob == DeferredJob::None || has_pending_responses()) {
        return;
    }
    if (millis() < g_deferredJobReadyAtMs) {
        return;
    }

    if (g_deferredJob == DeferredJob::DirectWriteRefresh) {
        bool ok = render_bitplane_image(g_directWriteState.black_plane, g_directWriteState.red_plane);
        reset_direct_write_state();
        g_deferredJob = DeferredJob::None;
        queue_simple_response(0x00, ok ? 0x73 : 0x74);
        return;
    }

    DashboardDataV1 dashboard_data;
    bool parsed = dashboard_parse_v1(g_dashboardContext.payload_buffer, g_dashboardContext.received_bytes, &dashboard_data);
    bool ok = false;
    if (parsed) {
        notify_dashboard_data(dashboard_data);
        if (g_requestedRefreshMode == app::kRefreshModeFast) {
            ok = render_dashboard_fast(dashboard_data);
            if (!ok) {
                ok = render_dashboard_full(dashboard_data);
            }
        } else {
            ok = render_dashboard_full(dashboard_data);
        }
    }
    dashboard_reset(g_dashboardContext);
    g_deferredJob = DeferredJob::None;
    queue_simple_response(0x00, ok ? 0x7B : 0x7C);
    if (ok) {
        mark_sleep_after_render();
    }
}
