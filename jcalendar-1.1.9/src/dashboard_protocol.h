#pragma once

#include <stdint.h>

constexpr uint16_t CMD_DASHBOARD_RENDER_START = 0x0078;
constexpr uint16_t CMD_DASHBOARD_RENDER_DATA = 0x0079;
constexpr uint16_t CMD_DASHBOARD_RENDER_COMMIT = 0x007A;
constexpr uint16_t CMD_DASHBOARD_NO_CHANGE_SLEEP = 0x007D;

constexpr uint8_t DASHBOARD_REFRESH_MODE_FULL = 0;
constexpr uint8_t DASHBOARD_REFRESH_MODE_FAST = 1;

constexpr uint8_t DASHBOARD_ERR_VERSION_UNSUPPORTED = 0x01;
constexpr uint8_t DASHBOARD_ERR_INVALID_LENGTH = 0x02;
constexpr uint8_t DASHBOARD_ERR_CRC_MISMATCH = 0x04;
constexpr uint8_t DASHBOARD_ERR_STATE_MACHINE = 0x05;
constexpr uint8_t DASHBOARD_ERR_DEVICE_BUSY = 0x07;
constexpr uint8_t DASHBOARD_ERR_REFRESH_MODE_UNSUPPORTED = 0x08;

constexpr uint32_t DASHBOARD_PAYLOAD_BUFFER_SIZE = 256;
constexpr uint32_t DASHBOARD_PAYLOAD_V1_SIZE = 192;

enum DashboardState : uint8_t {
    DASHBOARD_STATE_IDLE,
    DASHBOARD_STATE_RECEIVING,
    DASHBOARD_STATE_READY_TO_COMMIT,
    DASHBOARD_STATE_RENDERING,
};

struct DashboardProtocolContext {
    DashboardState state = DASHBOARD_STATE_IDLE;
    uint8_t payload_buffer[DASHBOARD_PAYLOAD_BUFFER_SIZE] = {};
    uint32_t expected_payload_len = 0;
    uint32_t received_bytes = 0;
    uint32_t declared_crc32 = 0;
    uint8_t schema_version = 0;
    uint8_t flags = 0;
};

void dashboard_protocol_init(DashboardProtocolContext& ctx);
uint16_t dashboard_handle_start(
    DashboardProtocolContext& ctx,
    const uint8_t* data,
    uint16_t len,
    uint8_t* response
);
uint16_t dashboard_handle_data(
    DashboardProtocolContext& ctx,
    const uint8_t* data,
    uint16_t len,
    uint8_t* response
);
uint16_t dashboard_handle_commit(
    DashboardProtocolContext& ctx,
    uint8_t refresh_mode,
    uint8_t* response,
    bool& should_render
);
void dashboard_reset(DashboardProtocolContext& ctx);
