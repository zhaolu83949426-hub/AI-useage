#pragma once

#include <stdint.h>

#include "dashboard_renderer.h"

void init_display_service();
bool render_bitplane_image(const uint8_t* black_plane, const uint8_t* red_plane);
bool render_dashboard(const DashboardDataV1& data);
