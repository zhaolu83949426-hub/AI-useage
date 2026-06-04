#include "dashboard_renderer.h"

#include <string.h>

bool dashboard_parse_v1(const uint8_t* payload, uint32_t len, DashboardDataV1* data) {
    if (!payload || !data || len < 192) {
        return false;
    }

    memset(data, 0, sizeof(*data));
    uint32_t offset = 0;
    data->schema_version = payload[offset++];
    data->row_count = payload[offset++];
    data->glm_5h_percent = payload[offset++];
    data->glm_week_percent = payload[offset++];
    data->gpt_5h_percent = payload[offset++];
    data->gpt_week_percent = payload[offset++];
    uint8_t glm_len = payload[offset++];
    offset++;

    data->total_tokens = payload[offset] | (payload[offset + 1] << 8) |
        (payload[offset + 2] << 16) | (payload[offset + 3] << 24);
    offset += 4;
    data->input_tokens = payload[offset] | (payload[offset + 1] << 8) |
        (payload[offset + 2] << 16) | (payload[offset + 3] << 24);
    offset += 4;
    data->output_tokens = payload[offset] | (payload[offset + 1] << 8) |
        (payload[offset + 2] << 16) | (payload[offset + 3] << 24);
    offset += 4;
    data->cache_tokens = payload[offset] | (payload[offset + 1] << 8) |
        (payload[offset + 2] << 16) | (payload[offset + 3] << 24);
    offset += 4;

    memcpy(data->last_refresh, payload + offset, 5);
    offset += 5;
    memcpy(data->glm_5h_label, payload + offset, 5);
    offset += 5;
    memcpy(data->glm_week_label, payload + offset, 5);
    offset += 5;
    memcpy(data->gpt_5h_label, payload + offset, 5);
    offset += 5;
    memcpy(data->gpt_week_label, payload + offset, 5);
    offset += 5;
    memcpy(data->glm_level, payload + offset, glm_len > 8 ? 8 : glm_len);
    offset += 8;

    for (uint8_t i = 0; i < data->row_count && i < 4; i++) {
        memcpy(data->models[i].model, payload + offset, 24);
        offset += 24;
        data->models[i].provider_code = payload[offset++];
        data->models[i].calls = payload[offset] | (payload[offset + 1] << 8);
        offset += 2;
        data->models[i].total_tokens = payload[offset] | (payload[offset + 1] << 8) |
            (payload[offset + 2] << 16) | (payload[offset + 3] << 24);
        offset += 4;
        data->models[i].share_bp = payload[offset] | (payload[offset + 1] << 8);
        offset += 2;
    }

    // 时间同步数据 (bytes 189-191)
    if (offset + 3 <= len) {
        data->sync_hour = payload[offset++];
        data->sync_minute = payload[offset++];
        data->sync_second = payload[offset++];
    }
    return true;
}
