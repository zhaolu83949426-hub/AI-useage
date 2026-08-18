#pragma once

#include <stdint.h>

#include "dashboard_renderer.h"

void init_display_service();
// 深度睡眠前让屏控进入 hibernate，避免仅 powerOff 时的待机漏电。
void prepare_display_for_sleep();
bool render_bitplane_image(const uint8_t* black_plane, const uint8_t* red_plane);
bool render_dashboard_full(const DashboardDataV1& data);
bool render_dashboard_fast(const DashboardDataV1& data);
