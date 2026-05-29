#include "dashboard_protocol.h"

#include <string.h>

namespace {

uint16_t build_error_response(uint8_t opcode, uint8_t error_code, uint8_t* response) {
    response[0] = 0xFF;
    response[1] = opcode;
    response[2] = error_code;
    response[3] = 0x00;
    return 4;
}

uint32_t crc32_update(uint32_t crc, uint8_t data) {
    crc ^= data;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
    return crc;
}

uint32_t crc32_buffer(const uint8_t* data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_update(crc, data[i]);
    }
    return crc ^ 0xFFFFFFFFUL;
}

}  // namespace

void dashboard_protocol_init(DashboardProtocolContext& ctx) {
    memset(&ctx, 0, sizeof(ctx));
    ctx.state = DASHBOARD_STATE_IDLE;
}

void dashboard_reset(DashboardProtocolContext& ctx) {
    ctx.state = DASHBOARD_STATE_IDLE;
    ctx.received_bytes = 0;
    ctx.expected_payload_len = 0;
    ctx.declared_crc32 = 0;
}

uint16_t dashboard_handle_start(
    DashboardProtocolContext& ctx,
    const uint8_t* data,
    uint16_t len,
    uint8_t* response
) {
    if (len < 8) {
        return build_error_response(0x78, DASHBOARD_ERR_INVALID_LENGTH, response);
    }
    if (ctx.state == DASHBOARD_STATE_RENDERING) {
        return build_error_response(0x78, DASHBOARD_ERR_DEVICE_BUSY, response);
    }

    dashboard_reset(ctx);
    if (data[0] != 1) {
        return build_error_response(0x78, DASHBOARD_ERR_VERSION_UNSUPPORTED, response);
    }

    ctx.flags = data[1];
    ctx.expected_payload_len = data[2] | (data[3] << 8);
    ctx.declared_crc32 = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    if (ctx.expected_payload_len != DASHBOARD_PAYLOAD_V1_SIZE) {
        dashboard_reset(ctx);
        return build_error_response(0x78, DASHBOARD_ERR_INVALID_LENGTH, response);
    }

    ctx.state = DASHBOARD_STATE_RECEIVING;
    ctx.schema_version = 1;
    response[0] = 0x00;
    response[1] = 0x78;
    return 2;
}

uint16_t dashboard_handle_data(
    DashboardProtocolContext& ctx,
    const uint8_t* data,
    uint16_t len,
    uint8_t* response
) {
    if (ctx.state != DASHBOARD_STATE_RECEIVING) {
        return build_error_response(0x79, DASHBOARD_ERR_STATE_MACHINE, response);
    }

    uint32_t remaining = ctx.expected_payload_len - ctx.received_bytes;
    if (len > remaining) {
        len = static_cast<uint16_t>(remaining);
    }
    memcpy(ctx.payload_buffer + ctx.received_bytes, data, len);
    ctx.received_bytes += len;
    if (ctx.received_bytes == ctx.expected_payload_len) {
        ctx.state = DASHBOARD_STATE_READY_TO_COMMIT;
    }

    response[0] = 0x00;
    response[1] = 0x79;
    return 2;
}

uint16_t dashboard_handle_commit(
    DashboardProtocolContext& ctx,
    uint8_t refresh_mode,
    uint8_t* response,
    bool& should_render
) {
    should_render = false;
    if (ctx.state != DASHBOARD_STATE_READY_TO_COMMIT) {
        return build_error_response(0x7A, DASHBOARD_ERR_STATE_MACHINE, response);
    }
    if (refresh_mode != 0) {
        return build_error_response(0x7A, DASHBOARD_ERR_REFRESH_MODE_UNSUPPORTED, response);
    }

    uint32_t actual_crc = crc32_buffer(ctx.payload_buffer, ctx.received_bytes);
    if (actual_crc != ctx.declared_crc32) {
        dashboard_reset(ctx);
        return build_error_response(0x7A, DASHBOARD_ERR_CRC_MISMATCH, response);
    }

    ctx.state = DASHBOARD_STATE_RENDERING;
    should_render = true;
    response[0] = 0x00;
    response[1] = 0x7A;
    return 2;
}
