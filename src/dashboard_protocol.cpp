#include "dashboard_protocol.h"
#include "dashboard_renderer.h"

#include <string.h>
#include "communication.h"

// Global protocol context
static DashboardProtocolContext g_dashboard_ctx;

// CRC32 implementation (standard polynomial 0xEDB88320)
static uint32_t crc32_update(uint32_t crc, uint8_t data) {
    crc ^= data;
    for (int i = 0; i < 8; i++) {
        if (crc & 1) {
            crc = (crc >> 1) ^ 0xEDB88320;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

static uint32_t crc32_buffer(const uint8_t* data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_update(crc, data[i]);
    }
    return crc ^ 0xFFFFFFFF;
}

static void send_error_response(uint8_t opcode, uint8_t error_code) {
    uint8_t response[] = {0xFF, opcode, error_code, 0x00};
    sendResponseUnencrypted(response, sizeof(response));
}

void dashboard_protocol_init(DashboardProtocolContext* ctx) {
    memset(ctx, 0, sizeof(DashboardProtocolContext));
    ctx->state = DASHBOARD_STATE_IDLE;
}

void dashboard_reset(DashboardProtocolContext* ctx) {
    ctx->state = DASHBOARD_STATE_IDLE;
    ctx->received_bytes = 0;
    ctx->expected_payload_len = 0;
    ctx->declared_crc32 = 0;
}

DashboardState dashboard_get_state(const DashboardProtocolContext* ctx) {
    return ctx->state;
}

uint16_t dashboard_handle_start(
    DashboardProtocolContext* ctx,
    const uint8_t* data,
    uint16_t len,
    uint8_t* response
) {
    // START format: [version:1][flags:1][payload_len_le:2][crc32_le:4][optional_initial_payload...]
    const uint16_t min_header_size = 8;  // 1+1+2+4

    if (len < min_header_size) {
        send_error_response(0x78, DASHBOARD_ERR_INVALID_LENGTH);
        return 0;
    }

    // If already rendering, reject
    if (ctx->state == DASHBOARD_STATE_RENDERING) {
        send_error_response(0x78, DASHBOARD_ERR_DEVICE_BUSY);
        return 0;
    }

    // Reset state for new transfer (always allow restart)
    dashboard_reset(ctx);

    // Parse header
    uint8_t version = data[0];
    ctx->flags = data[1];
    ctx->expected_payload_len = data[2] | (data[3] << 8);
    ctx->declared_crc32 = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);

    // Validate version
    if (version != 1) {
        send_error_response(0x78, DASHBOARD_ERR_VERSION_UNSUPPORTED);
        dashboard_reset(ctx);
        return 0;
    }

    // Validate payload length
    if (ctx->expected_payload_len != DASHBOARD_PAYLOAD_V1_SIZE) {
        send_error_response(0x78, DASHBOARD_ERR_INVALID_LENGTH);
        dashboard_reset(ctx);
        return 0;
    }

    // Check buffer capacity
    if (ctx->expected_payload_len > DASHBOARD_PAYLOAD_BUFFER_SIZE) {
        send_error_response(0x78, DASHBOARD_ERR_PAYLOAD_TOO_LARGE);
        dashboard_reset(ctx);
        return 0;
    }

    ctx->schema_version = version;
    ctx->state = DASHBOARD_STATE_RECEIVING;

    // Copy initial payload if present
    uint16_t initial_len = len - min_header_size;
    if (initial_len > 0) {
        if (initial_len > ctx->expected_payload_len) {
            initial_len = ctx->expected_payload_len;
        }
        memcpy(ctx->payload_buffer, data + min_header_size, initial_len);
        ctx->received_bytes = initial_len;
    }

    // Send ACK
    response[0] = 0x00;
    response[1] = 0x78;
    return 2;
}

uint16_t dashboard_handle_data(
    DashboardProtocolContext* ctx,
    const uint8_t* data,
    uint16_t len,
    uint8_t* response
) {
    // Validate state
    if (ctx->state != DASHBOARD_STATE_RECEIVING) {
        send_error_response(0x79, DASHBOARD_ERR_STATE_MACHINE);
        return 0;
    }

    // Calculate remaining space
    uint32_t remaining = ctx->expected_payload_len - ctx->received_bytes;
    uint16_t to_copy = (len < remaining) ? len : remaining;

    // Copy data
    memcpy(ctx->payload_buffer + ctx->received_bytes, data, to_copy);
    ctx->received_bytes += to_copy;

    // Check if reception complete
    if (ctx->received_bytes >= ctx->expected_payload_len) {
        ctx->state = DASHBOARD_STATE_READY_TO_COMMIT;
    }

    // Send ACK
    response[0] = 0x00;
    response[1] = 0x79;
    return 2;
}

uint16_t dashboard_handle_commit(
    DashboardProtocolContext* ctx,
    const uint8_t* data,
    uint16_t len,
    uint8_t* response,
    uint8_t refresh_mode,
    bool* out_should_render
) {
    // Validate state
    if (ctx->state != DASHBOARD_STATE_READY_TO_COMMIT) {
        send_error_response(0x7A, DASHBOARD_ERR_STATE_MACHINE);
        return 0;
    }

    // Validate refresh mode (only FULL supported in v1)
    if (refresh_mode != 0) {
        send_error_response(0x7A, DASHBOARD_ERR_REFRESH_MODE_UNSUPPORTED);
        return 0;
    }

    // Verify CRC32
    uint32_t calculated_crc = crc32_buffer(ctx->payload_buffer, ctx->received_bytes);
    if (calculated_crc != ctx->declared_crc32) {
        send_error_response(0x7A, DASHBOARD_ERR_CRC_MISMATCH);
        dashboard_reset(ctx);
        return 0;
    }

    // Move to RENDERING state
    ctx->state = DASHBOARD_STATE_RENDERING;

    // Send ACK — caller must send this before invoking render
    response[0] = 0x00;
    response[1] = 0x7A;

    if (out_should_render) *out_should_render = true;
    return 2;
}

void dashboard_execute_render(DashboardProtocolContext* ctx) {
    DashboardDataV1 dashboard_data;
    if (dashboard_parse_v1(ctx->payload_buffer, ctx->received_bytes, &dashboard_data)) {
        dashboard_render(&dashboard_data);
    }
    dashboard_reset(ctx);
}

// Get global context (for communication.cpp)
extern "C" DashboardProtocolContext* get_dashboard_context(void) {
    return &g_dashboard_ctx;
}
