#pragma once

#include <stdint.h>

typedef struct {
    uint8_t schema_version;
    uint8_t row_count;
    uint8_t glm_5h_percent;
    uint8_t glm_week_percent;
    uint8_t gpt_5h_percent;
    uint8_t gpt_week_percent;
    char glm_level[9];
    uint32_t total_tokens;
    uint32_t input_tokens;
    uint32_t output_tokens;
    uint32_t cache_tokens;
    char last_refresh[6];
    char glm_5h_label[6];
    char glm_week_label[6];
    char gpt_5h_label[6];
    char gpt_week_label[6];
    struct {
        char model[25];
        uint8_t provider_code;
        uint16_t calls;
        uint32_t total_tokens;
        uint16_t share_bp;
    } models[4];
    uint8_t sync_hour;
    uint8_t sync_minute;
    uint8_t sync_flags;
} DashboardDataV1;

bool dashboard_parse_v1(const uint8_t* payload, uint32_t len, DashboardDataV1* data);
