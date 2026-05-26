#ifndef DASHBOARD_PROTOCOL_H
#define DASHBOARD_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Dashboard render command opcodes
#define CMD_DASHBOARD_RENDER_START    0x0078
#define CMD_DASHBOARD_RENDER_DATA     0x0079
#define CMD_DASHBOARD_RENDER_COMMIT   0x007A

// Response opcodes
#define RESP_DASHBOARD_RENDER_START_ACK   0x0078
#define RESP_DASHBOARD_RENDER_DATA_ACK    0x0079
#define RESP_DASHBOARD_RENDER_COMMIT_ACK  0x007A
#define RESP_DASHBOARD_RENDER_SUCCESS     0x007B
#define RESP_DASHBOARD_RENDER_TIMEOUT     0x007C

// Error codes
#define DASHBOARD_ERR_VERSION_UNSUPPORTED    0x01
#define DASHBOARD_ERR_INVALID_LENGTH        0x02
#define DASHBOARD_ERR_PAYLOAD_TOO_LARGE     0x03
#define DASHBOARD_ERR_CRC_MISMATCH          0x04
#define DASHBOARD_ERR_STATE_MACHINE         0x05
#define DASHBOARD_ERR_INVALID_FIELD         0x06
#define DASHBOARD_ERR_DEVICE_BUSY           0x07
#define DASHBOARD_ERR_REFRESH_MODE_UNSUPPORTED 0x08

// Protocol constants
#define DASHBOARD_PAYLOAD_BUFFER_SIZE 256  // 192B payload + room for headers
#define DASHBOARD_PAYLOAD_V1_SIZE      192  // DashboardSnapshotV1 fixed size

// State machine states
typedef enum {
    DASHBOARD_STATE_IDLE,
    DASHBOARD_STATE_RECEIVING,
    DASHBOARD_STATE_READY_TO_COMMIT,
    DASHBOARD_STATE_RENDERING,
} DashboardState;

// Protocol context
typedef struct {
    DashboardState state;
    uint8_t payload_buffer[DASHBOARD_PAYLOAD_BUFFER_SIZE];
    uint32_t expected_payload_len;
    uint32_t received_bytes;
    uint32_t declared_crc32;
    uint8_t schema_version;
    uint8_t flags;
} DashboardProtocolContext;

// Initialize the protocol context
void dashboard_protocol_init(DashboardProtocolContext* ctx);

// Handle START command (0x0078)
// Returns response length (0 for no response, >0 for error response)
uint16_t dashboard_handle_start(
    DashboardProtocolContext* ctx,
    const uint8_t* data,
    uint16_t len,
    uint8_t* response
);

// Handle DATA command (0x0079)
// Returns response length (0 for no response, >0 for error response)
uint16_t dashboard_handle_data(
    DashboardProtocolContext* ctx,
    const uint8_t* data,
    uint16_t len,
    uint8_t* response
);

// Handle COMMIT command (0x007A)
// Validates CRC and returns COMMIT ACK. Does NOT render.
// Sets *out_should_render = true if caller should invoke dashboard_execute_render().
uint16_t dashboard_handle_commit(
    DashboardProtocolContext* ctx,
    const uint8_t* data,
    uint16_t len,
    uint8_t* response,
    uint8_t refresh_mode,
    bool* out_should_render
);

// Execute the actual rendering after COMMIT ACK has been sent.
// Resets state machine to IDLE when done.
void dashboard_execute_render(DashboardProtocolContext* ctx);

// Get current state (for debugging)
DashboardState dashboard_get_state(const DashboardProtocolContext* ctx);

// Reset state machine to IDLE
void dashboard_reset(DashboardProtocolContext* ctx);

#ifdef __cplusplus
}
#endif

#endif // DASHBOARD_PROTOCOL_H
