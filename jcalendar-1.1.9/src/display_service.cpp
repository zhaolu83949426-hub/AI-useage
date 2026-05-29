#include "display_service.h"

#include <Arduino.h>
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "GxEPD2_display_selection_new_style.h"
#include "app_config.h"

namespace {

GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, app::kDisplayPageHeight> display(
    GxEPD2_DRIVER_CLASS(app::kPinCs, app::kPinDc, app::kPinReset, app::kPinBusy)
);
U8G2_FOR_ADAFRUIT_GFX g_fonts;

constexpr uint16_t kWhite = GxEPD_WHITE;
constexpr uint16_t kBlack = GxEPD_BLACK;
constexpr uint16_t kRed = GxEPD_RED;
constexpr int kBatteryVoltageMaxMv = 4200;
constexpr int kBatteryVoltageMinMv = 3300;

#define FONT_LABEL u8g2_font_helvB10_tf
#define FONT_VALUE u8g2_font_helvB14_tf
#define FONT_BIG u8g2_font_helvB24_tf
#define FONT_SMALL u8g2_font_helvB10_tf
#define FONT_BOOT u8g2_font_helvB14_tf

void set_font(const uint8_t* font, uint16_t color) {
    g_fonts.setFontMode(1);
    g_fonts.setFontDirection(0);
    g_fonts.setForegroundColor(color);
    g_fonts.setBackgroundColor(kWhite);
    g_fonts.setFont(font);
}

void draw_text(int16_t x, int16_t y, const char* text, const uint8_t* font, uint16_t color) {
    set_font(font, color);
    g_fonts.setCursor(x, y);
    g_fonts.print(text);
}

int16_t text_width(const char* text, const uint8_t* font) {
    set_font(font, kBlack);
    return g_fonts.getUTF8Width(text);
}

void format_tokens(char* buffer, uint32_t value) {
    if (value >= 1000000) {
        snprintf(buffer, 16, "%.1fM", value / 1000000.0);
        return;
    }
    if (value >= 1000) {
        snprintf(buffer, 16, "%.1fK", value / 1000.0);
        return;
    }
    snprintf(buffer, 16, "%lu", static_cast<unsigned long>(value));
}

void format_percent(char* buffer, uint16_t basis_points) {
    snprintf(buffer, 8, "%.1f%%", basis_points / 100.0);
}

uint8_t read_battery_percent() {
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

    int voltage_mv = static_cast<int>((adc_sum / 10.0f) * app::kBatteryVoltageScalingFactor * 10.0f);
    if (voltage_mv >= kBatteryVoltageMaxMv) {
        return 100;
    }
    if (voltage_mv <= kBatteryVoltageMinMv) {
        return 0;
    }
    return static_cast<uint8_t>(
        (voltage_mv - kBatteryVoltageMinMv) * 100 / (kBatteryVoltageMaxMv - kBatteryVoltageMinMv)
    );
}

void draw_overview(const DashboardDataV1& data) {
    char total[16], input_s[16], output_s[16], cache_s[16];
    format_tokens(total, data.total_tokens);
    format_tokens(input_s, data.input_tokens);
    format_tokens(output_s, data.output_tokens);
    format_tokens(cache_s, data.cache_tokens);

    display.drawRoundRect(8, 8, 384, 80, 4, kBlack);
    display.drawLine(152, 16, 152, 80, kBlack);
    display.drawLine(232, 16, 232, 80, kBlack);
    display.drawLine(312, 16, 312, 80, kBlack);

    int16_t tw = text_width("TOTAL", FONT_LABEL);
    draw_text(8 + (144 - tw) / 2, 30, "TOTAL", FONT_LABEL, kBlack);
    tw = text_width("INPUT", FONT_LABEL);
    draw_text(152 + (80 - tw) / 2, 30, "INPUT", FONT_LABEL, kBlack);
    tw = text_width("OUTPUT", FONT_LABEL);
    draw_text(232 + (80 - tw) / 2, 30, "OUTPUT", FONT_LABEL, kBlack);
    tw = text_width("CACHE", FONT_LABEL);
    draw_text(312 + (80 - tw) / 2, 30, "CACHE", FONT_LABEL, kBlack);

    tw = text_width(total, FONT_BIG);
    draw_text(8 + (144 - tw) / 2, 74, total, FONT_BIG, kBlack);
    tw = text_width(input_s, FONT_VALUE);
    draw_text(152 + (80 - tw) / 2, 70, input_s, FONT_VALUE, kBlack);
    tw = text_width(output_s, FONT_VALUE);
    draw_text(232 + (80 - tw) / 2, 70, output_s, FONT_VALUE, kBlack);
    tw = text_width(cache_s, FONT_VALUE);
    draw_text(312 + (80 - tw) / 2, 70, cache_s, FONT_VALUE, kBlack);
}

void draw_progress_row_5h(int16_t left, int16_t top, const char* label,
                          uint8_t percent, const char* time_text) {
    char pct_text[8];
    format_percent(pct_text, static_cast<uint16_t>(percent) * 100);

    draw_text(left + 6, top + 13, label, FONT_SMALL, kBlack);
    display.drawRoundRect(left + 30, top + 2, 74, 12, 2, kBlack);
    int fill_width = ((74 - 2) * percent) / 100;
    if (fill_width > 0) {
        display.fillRect(left + 31, top + 3, fill_width, 10, kBlack);
    }

    draw_text(left + 105, top + 13, pct_text, FONT_SMALL, kBlack);
    draw_text(left + 145, top + 13, time_text, FONT_SMALL, kBlack);
}

void draw_progress_row_1w(int16_t left, int16_t top, const char* label,
                          uint8_t percent, const char* date_text) {
    char pct_text[8];
    format_percent(pct_text, static_cast<uint16_t>(percent) * 100);

    draw_text(left + 6, top + 13, label, FONT_SMALL, kBlack);
    display.drawRoundRect(left + 30, top + 2, 74, 12, 2, kBlack);
    int fill_width = ((74 - 2) * percent) / 100;
    if (fill_width > 0) {
        display.fillRect(left + 31, top + 3, fill_width, 10, kBlack);
    }

    draw_text(left + 105, top + 13, pct_text, FONT_SMALL, kBlack);
    draw_text(left + 150, top + 13, date_text, FONT_SMALL, kBlack);
}

void draw_plan_cards(const DashboardDataV1& data) {
    char glm_title[24];
    snprintf(glm_title, sizeof(glm_title), data.glm_level[0] ? "GLM %s" : "GLM", data.glm_level);

    display.drawRoundRect(8, 93, 188, 71, 4, kBlack);
    display.drawRoundRect(204, 93, 188, 71, 4, kBlack);

    draw_text(16, 112, glm_title, FONT_VALUE, kBlack);
    draw_text(212, 112, "GPT Plus", FONT_VALUE, kBlack);
    draw_progress_row_5h(8, 119, "5H", data.glm_5h_percent, data.glm_5h_label);
    draw_progress_row_1w(8, 141, "1W", data.glm_week_percent, data.glm_week_label);
    draw_progress_row_5h(204, 119, "5H", data.gpt_5h_percent, data.gpt_5h_label);
    draw_progress_row_1w(204, 141, "1W", data.gpt_week_percent, data.gpt_week_label);
}

void trim_model_name(const char* source, char* target, size_t size) {
    size_t length = strlen(source);
    if (length < size) {
        memcpy(target, source, length + 1);
        return;
    }
    memcpy(target, source, size - 1);
    target[size - 1] = '\0';
}

void draw_model_table(const DashboardDataV1& data) {
    display.drawRoundRect(8, 166, 384, 106, 4, kBlack);
    display.drawLine(8, 188, 392, 188, kBlack);
    display.drawLine(100, 188, 100, 272, kBlack);
    display.drawLine(156, 188, 156, 272, kBlack);
    display.drawLine(242, 188, 242, 272, kBlack);

    draw_text(14, 183, "MODEL", FONT_LABEL, kBlack);
    draw_text(108, 183, "CALLS", FONT_LABEL, kBlack);
    draw_text(164, 183, "TOKEN", FONT_LABEL, kBlack);
    draw_text(250, 183, "SHARE", FONT_LABEL, kBlack);

    for (uint8_t i = 0; i < data.row_count && i < 4; i++) {
        char name[17], token_text[16], share_text[8], calls_text[8];
        trim_model_name(data.models[i].model, name, sizeof(name));
        format_tokens(token_text, data.models[i].total_tokens);
        format_percent(share_text, data.models[i].share_bp);
        snprintf(calls_text, sizeof(calls_text), "%u", data.models[i].calls);

        int row_top = 192 + i * 20;
        if (i > 0) {
            display.drawLine(8, row_top - 2, 392, row_top - 2, kBlack);
        }

        draw_text(14, row_top + 12, name, FONT_SMALL, kBlack);
        draw_text(108, row_top + 12, calls_text, FONT_SMALL, kBlack);
        draw_text(164, row_top + 12, token_text, FONT_SMALL, kBlack);

        display.drawRoundRect(250, row_top + 3, 72, 8, 2, kBlack);
        int fill_width = (72 - 2) * (data.models[i].share_bp / 100.0f) / 100.0f;
        if (fill_width > 0) {
            display.fillRect(251, row_top + 4, fill_width, 6, kBlack);
        }
        draw_text(330, row_top + 12, share_text, FONT_SMALL, kBlack);
    }
}

void draw_footer(const DashboardDataV1& data) {
    char footer[32];
    snprintf(footer, sizeof(footer), "LAST: %s", data.last_refresh);
    draw_text(12, 292, footer, FONT_SMALL, kBlack);

    uint8_t battery_pct = read_battery_percent();
    char battery_text[16];
    snprintf(battery_text, sizeof(battery_text), "BAT:%u%%", battery_pct);
    int16_t battery_width = text_width(battery_text, FONT_SMALL);
    draw_text(392 - battery_width - 8, 292, battery_text, FONT_SMALL, kBlack);
}

void draw_dashboard_page(const DashboardDataV1& data) {
    display.fillScreen(kWhite);
    draw_overview(data);
    draw_plan_cards(data);
    draw_model_table(data);
    draw_footer(data);
}

void finish_refresh() {
    display.powerOff();
    display.hibernate();
}

}  // namespace

void init_display_service() {
    display.init(115200);
    display.setRotation(0);
    g_fonts.begin(display);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(kWhite);
        draw_text(68, 155, "OpenDisplay Dashboard", FONT_BOOT, kBlack);
    } while (display.nextPage());
    finish_refresh();
}

bool render_bitplane_image(const uint8_t* black_plane, const uint8_t* red_plane) {
    if (!black_plane || !red_plane) {
        return false;
    }
    display.setFullWindow();
    display.drawImage(black_plane, red_plane, 0, 0, app::kDisplayWidth, app::kDisplayHeight);
    finish_refresh();
    return true;
}

bool render_dashboard(const DashboardDataV1& data) {
    // 这里保持原厂驱动的整屏刷新路径，只替换上层看板绘制内容。
    display.setFullWindow();
    display.firstPage();
    do {
        draw_dashboard_page(data);
    } while (display.nextPage());
    finish_refresh();
    return true;
}
