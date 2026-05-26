#ifndef DASHBOARD_RENDERER_H
#define DASHBOARD_RENDERER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Parsed DashboardSnapshotV1 data
typedef struct {
    // Header
    uint8_t schema_version;
    uint8_t row_count;

    // Percentages
    uint8_t glm_5h_percent;
    uint8_t glm_week_percent;
    uint8_t gpt_5h_percent;
    uint8_t gpt_week_percent;

    // GLM plan level
    char glm_level[9];  // Max 8 + null terminator

    // Token counts
    uint32_t total_tokens;
    uint32_t input_tokens;
    uint32_t output_tokens;
    uint32_t cache_tokens;

    // Time labels
    char last_refresh[6];    // HH:mm + null
    char glm_5h_label[6];
    char glm_week_label[6];
    char gpt_5h_label[6];
    char gpt_week_label[6];

    // Models (up to 4)
    struct {
        char model[25];          // Max 24 + null
        uint8_t provider_code;
        uint16_t calls;
        uint32_t total_tokens;
        uint16_t share_bp;       // Basis points (0-10000)
    } models[4];
} DashboardDataV1;

// Parse DashboardSnapshotV1 from binary payload
// Returns true if parsing succeeded
bool dashboard_parse_v1(const uint8_t* payload, uint32_t len, DashboardDataV1* data);

// Render dashboard to display
// This will:
// 1. Clear the display
// 2. Draw the fixed template layout
// 3. Fill in dynamic data from DashboardDataV1
// 4. Trigger full refresh
void dashboard_render(const DashboardDataV1* data);

// Format token count for display (e.g., 71.5M, 55.4K, 999)
void dashboard_format_tokens(char* buf, uint32_t tokens);

// Format percentage with decimal (e.g., "49.8%")
void dashboard_format_percent(char* buf, uint16_t share_bp);

// Get provider name from code
const char* dashboard_get_provider_name(uint8_t provider_code);

#ifdef __cplusplus
}
#endif

#endif // DASHBOARD_RENDERER_H
