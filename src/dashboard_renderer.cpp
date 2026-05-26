#include "dashboard_renderer.h"
#include "display_service.h"
#include "touch_input.h"
#include "structs.h"

#include <Arduino.h>
#include <bb_epaper.h>
#include <string.h>
#include <stdio.h>

// Display layout
#define DW 400
#define DH 300
#define PITCH ((DW + 7) / 8)  // 50 bytes per row for 1BPP

extern BBEPDISP bbep;
extern struct GlobalConfig globalConfig;
extern uint8_t staticRowBuffer[680];
extern bool displayPowerState;
void pwrmgm(bool onoff);
void bbepInitIO(BBEPDISP*, uint8_t dc, uint8_t rst, uint8_t busy, uint8_t cs, uint8_t mosi, uint8_t sck, uint32_t speed);
void bbepWakeUp(BBEPDISP*);
void bbepSendCMDSequence(BBEPDISP*, const uint8_t*);
void bbepRefresh(BBEPDISP*, int);
void bbepSleep(BBEPDISP*, int);
void bbepSetAddrWindow(BBEPDISP*, int x, int y, int cx, int cy);
void bbepStartWrite(BBEPDISP*, int iPlane);
void bbepWriteData(BBEPDISP*, uint8_t*, int);
void writeSerial(String, bool);
#ifdef TARGET_ESP32
void esp32_restart_ble_advertising();
#endif

// --- Extended 5x7 font ---
typedef struct { char c; uint8_t col[5]; } Glyph5x7;

static const Glyph5x7 FONT[] = {
    {' ', {0,0,0,0,0}},
    {'-', {0,0,0x7E,0,0}}, {'.', {0,0,0x40,0,0}},
    {':', {0,0x36,0,0x36,0}}, {'/', {0x08,0x08,0x14,0x22,0x41}},
    {'%', {0x62,0x14,0x08,0x14,0x46}},
    {'0', {0x3E,0x51,0x49,0x45,0x3E}}, {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x62,0x51,0x49,0x49,0x46}}, {'3', {0x22,0x49,0x49,0x49,0x36}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}}, {'5', {0x2F,0x49,0x49,0x49,0x31}},
    {'6', {0x3E,0x49,0x49,0x49,0x32}}, {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}}, {'9', {0x26,0x49,0x49,0x49,0x3E}},
    {'A', {0x7E,0x11,0x11,0x11,0x7E}}, {'B', {0x7F,0x49,0x49,0x49,0x36}},
    {'C', {0x3E,0x41,0x41,0x41,0x22}}, {'D', {0x7F,0x41,0x41,0x22,0x1C}},
    {'E', {0x7F,0x49,0x49,0x49,0x41}}, {'F', {0x7F,0x09,0x09,0x09,0x01}},
    {'G', {0x3E,0x41,0x49,0x49,0x7A}}, {'H', {0x7F,0x08,0x08,0x08,0x7F}},
    {'I', {0x00,0x41,0x7F,0x41,0x00}}, {'J', {0x20,0x40,0x41,0x3F,0x01}},
    {'K', {0x7F,0x08,0x14,0x22,0x41}}, {'L', {0x7F,0x40,0x40,0x40,0x40}},
    {'M', {0x7F,0x02,0x0C,0x02,0x7F}}, {'N', {0x7F,0x02,0x0C,0x10,0x7F}},
    {'O', {0x3E,0x41,0x41,0x41,0x3E}}, {'P', {0x7F,0x09,0x09,0x09,0x06}},
    {'Q', {0x3E,0x41,0x51,0x21,0x5E}}, {'R', {0x7F,0x09,0x19,0x29,0x46}},
    {'S', {0x26,0x49,0x49,0x49,0x32}}, {'T', {0x01,0x01,0x7F,0x01,0x01}},
    {'U', {0x3F,0x40,0x40,0x40,0x3F}}, {'V', {0x0F,0x30,0x40,0x30,0x0F}},
    {'W', {0x3F,0x40,0x38,0x40,0x3F}}, {'X', {0x63,0x14,0x08,0x14,0x63}},
    {'Y', {0x07,0x08,0x70,0x08,0x07}}, {'Z', {0x61,0x51,0x49,0x45,0x43}},
    {'a', {0x20,0x54,0x54,0x54,0x78}}, {'b', {0x7F,0x48,0x44,0x44,0x38}},
    {'c', {0x38,0x44,0x44,0x44,0x20}}, {'d', {0x38,0x44,0x44,0x48,0x7F}},
    {'e', {0x38,0x54,0x54,0x54,0x18}}, {'f', {0x08,0x7E,0x09,0x01,0x02}},
    {'g', {0x0C,0x52,0x52,0x52,0x3E}}, {'h', {0x7F,0x08,0x04,0x04,0x78}},
    {'i', {0x00,0x44,0x7D,0x40,0x00}}, {'j', {0x20,0x40,0x44,0x3D,0x00}},
    {'k', {0x7F,0x10,0x28,0x44,0x00}}, {'l', {0x00,0x41,0x7F,0x40,0x00}},
    {'m', {0x7C,0x04,0x18,0x04,0x78}}, {'n', {0x7C,0x08,0x04,0x04,0x78}},
    {'o', {0x38,0x44,0x44,0x44,0x38}}, {'p', {0x7C,0x14,0x14,0x14,0x08}},
    {'q', {0x08,0x14,0x14,0x18,0x7C}}, {'r', {0x7C,0x08,0x04,0x04,0x08}},
    {'s', {0x48,0x54,0x54,0x54,0x20}}, {'t', {0x04,0x3F,0x44,0x40,0x20}},
    {'u', {0x3C,0x40,0x40,0x20,0x7C}}, {'v', {0x1C,0x20,0x40,0x20,0x1C}},
    {'w', {0x3C,0x40,0x30,0x40,0x3C}}, {'x', {0x44,0x28,0x10,0x28,0x44}},
    {'y', {0x0C,0x50,0x50,0x50,0x3C}}, {'z', {0x44,0x64,0x54,0x4C,0x44}},
};

static const Glyph5x7* findGlyph(char c) {
    for (unsigned i = 0; i < sizeof(FONT) / sizeof(FONT[0]); i++) {
        if (FONT[i].c == c) return &FONT[i];
    }
    return &FONT[0];  // space for unknown
}

// --- Pixel ops ---
static inline void setPixelBlack(uint8_t* row, uint16_t x) {
    row[x / 8] &= ~(1 << (7 - (x % 8)));
}

static inline void setPixelRed(uint8_t* row, uint16_t x) {
    row[x / 8] |= (1 << (7 - (x % 8)));
}

// --- Text width ---
static uint16_t textWidth(const char* s, uint8_t scale) {
    if (!s || !scale) return 0;
    return (uint16_t)(strlen(s) * 6U * scale);
}

// --- Text drawing on a single row (black plane) ---
static void drawTextRow(uint8_t* row, uint16_t y, uint16_t x0, uint16_t y0,
                        const char* s, uint8_t scale) {
    if (!s || !scale) return;
    uint16_t cursor = x0;
    for (const char* p = s; *p; p++) {
        const uint8_t* g = findGlyph(*p)->col;
        for (uint8_t col = 0; col < 5; col++) {
            uint8_t bits = g[col];
            for (uint8_t gy = 0; gy < 7; gy++) {
                if (!((bits >> gy) & 1)) continue;
                uint16_t py = y0 + gy * scale;
                if (y < py || y >= py + scale) continue;
                uint16_t px = cursor + col * scale;
                for (uint8_t sx = 0; sx < scale; sx++) {
                    if (px + sx < DW) setPixelBlack(row, px + sx);
                }
            }
        }
        cursor += 6 * scale;
    }
}

// --- Text drawing on red plane ---
static void drawTextRowRed(uint8_t* row, uint16_t y, uint16_t x0, uint16_t y0,
                           const char* s, uint8_t scale) {
    if (!s || !scale) return;
    uint16_t cursor = x0;
    for (const char* p = s; *p; p++) {
        const uint8_t* g = findGlyph(*p)->col;
        for (uint8_t col = 0; col < 5; col++) {
            uint8_t bits = g[col];
            for (uint8_t gy = 0; gy < 7; gy++) {
                if (!((bits >> gy) & 1)) continue;
                uint16_t py = y0 + gy * scale;
                if (y < py || y >= py + scale) continue;
                uint16_t px = cursor + col * scale;
                for (uint8_t sx = 0; sx < scale; sx++) {
                    if (px + sx < DW) setPixelRed(row, px + sx);
                }
            }
        }
        cursor += 6 * scale;
    }
}

// --- Drawing primitives ---
static void drawHLine(uint8_t* row, uint16_t y, uint16_t x0, uint16_t x1, uint16_t ly) {
    if (y != ly) return;
    for (uint16_t x = x0; x <= x1 && x < DW; x++) setPixelBlack(row, x);
}

static void drawVLine(uint8_t* row, uint16_t y, uint16_t x, uint16_t yt, uint16_t yb) {
    if (y < yt || y > yb) return;
    if (x < DW) setPixelBlack(row, x);
}

static void drawRect(uint8_t* row, uint16_t y, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    if (y < y0 || y > y1) return;
    drawHLine(row, y, x0, x1, y0);
    drawHLine(row, y, x0, x1, y1);
    drawVLine(row, y, x0, y0, y1);
    drawVLine(row, y, x1, y0, y1);
}

static void fillRectBlack(uint8_t* row, uint16_t y, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    if (y < y0 || y > y1) return;
    for (uint16_t x = x0; x <= x1 && x < DW; x++) setPixelBlack(row, x);
}

static void fillRectRed(uint8_t* redRow, uint16_t y, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    if (y < y0 || y > y1) return;
    for (uint16_t x = x0; x <= x1 && x < DW; x++) setPixelRed(redRow, x);
}

static void drawDashedVLine(uint8_t* row, uint16_t y, uint16_t x, uint16_t yt, uint16_t yb) {
    if (y < yt || y > yb) return;
    if (((y - yt) % 4) < 2) return;
    if (x < DW) setPixelBlack(row, x);
}

// Rounded rect (simplified: just rect, no corners for v1)
static void drawRRect(uint8_t* row, uint16_t y, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    drawRect(row, y, x0, y0, x1, y1);
}

// Progress bar (outline + fill)
static void drawProgressBar(uint8_t* bwRow, uint8_t* redRow, uint16_t y,
                            uint16_t x, uint16_t barW, uint16_t barH,
                            uint16_t y0, uint8_t pct, bool alert) {
    int16_t yt = y0, yb = y0 + barH - 1;
    // outline
    drawRect(bwRow, y, x, yt, x + barW, yb);
    // fill
    int fillW = (int)((barW - 2) * pct / 100);
    if (fillW < 0) fillW = 0;
    if (fillW > barW - 2) fillW = barW - 2;
    if (y > yt && y < yb && fillW > 0) {
        for (int i = 0; i < fillW; i++) {
            uint16_t px = x + 1 + i;
            if (px < DW) {
                if (alert) setPixelRed(redRow, px);
                else setPixelBlack(bwRow, px);
            }
        }
    }
}

// --- Provider ---
static const char* getProviderTag(uint8_t code) {
    switch (code) {
        case 1: return "ZHI";
        case 2: return "OPN";
        case 3: return "ANT";
        case 4: return "GOO";
        default: return "OTH";
    }
}

// --- Format helpers ---
void dashboard_format_tokens(char* buf, uint32_t tokens) {
    if (tokens >= 1000000) sprintf(buf, "%.1fM", (double)tokens / 1000000.0);
    else if (tokens >= 1000) sprintf(buf, "%.1fK", (double)tokens / 1000.0);
    else sprintf(buf, "%u", (unsigned)tokens);
}

void dashboard_format_percent(char* buf, uint16_t share_bp) {
    sprintf(buf, "%.1f%%", (double)share_bp / 100.0);
}

const char* dashboard_get_provider_name(uint8_t code) { return getProviderTag(code); }

// --- Parse ---
bool dashboard_parse_v1(const uint8_t* payload, uint32_t len, DashboardDataV1* data) {
    if (len < 192 || !payload || !data) return false;
    memset(data, 0, sizeof(DashboardDataV1));
    uint32_t o = 0;
    data->schema_version = payload[o++];
    data->row_count = payload[o++];
    data->glm_5h_percent = payload[o++];
    data->glm_week_percent = payload[o++];
    data->gpt_5h_percent = payload[o++];
    data->gpt_week_percent = payload[o++];
    uint8_t glmLen = payload[o++];
    o++; // reserved
    data->total_tokens = payload[o]|(payload[o+1]<<8)|(payload[o+2]<<16)|(payload[o+3]<<24); o+=4;
    data->input_tokens = payload[o]|(payload[o+1]<<8)|(payload[o+2]<<16)|(payload[o+3]<<24); o+=4;
    data->output_tokens = payload[o]|(payload[o+1]<<8)|(payload[o+2]<<16)|(payload[o+3]<<24); o+=4;
    data->cache_tokens = payload[o]|(payload[o+1]<<8)|(payload[o+2]<<16)|(payload[o+3]<<24); o+=4;
    #define CP_LABEL(field) memcpy(data->field, payload+o, 5); data->field[5]='\0'; o+=5;
    CP_LABEL(last_refresh); CP_LABEL(glm_5h_label); CP_LABEL(glm_week_label);
    CP_LABEL(gpt_5h_label); CP_LABEL(gpt_week_label);
    memcpy(data->glm_level, payload+o, glmLen); data->glm_level[glmLen]='\0'; o+=8;
    for (uint8_t i = 0; i < 4 && i < data->row_count; i++) {
        memcpy(data->models[i].model, payload+o, 24); data->models[i].model[24]='\0'; o+=24;
        data->models[i].provider_code = payload[o++];
        data->models[i].calls = payload[o]|(payload[o+1]<<8); o+=2;
        data->models[i].total_tokens = payload[o]|(payload[o+1]<<8)|(payload[o+2]<<16)|(payload[o+3]<<24); o+=4;
        data->models[i].share_bp = payload[o]|(payload[o+1]<<8); o+=2;
    }
    return true;
}

// ===================== MAIN RENDER =====================

// Layout regions
#define OX1 8
#define OY1 8
#define OX2 392
#define OY2 88
#define OXW (OX2-OX1)  // 384

#define GLM_X1 8
#define GLM_Y1 93
#define GLM_X2 196
#define GLM_Y2 164

#define GPT_X1 204
#define GPT_Y1 93
#define GPT_X2 392
#define GPT_Y2 164

#define TBL_X1 8
#define TBL_Y1 166
#define TBL_X2 392
#define TBL_Y2 272

#define OVERVIEW_COL0_W 144
#define OVERVIEW_COL_W 80
#define OVERVIEW_TITLE_SCALE 2
#define OVERVIEW_VALUE_SCALE 2
#define OVERVIEW_TOTAL_VALUE_SCALE 3

#define PLAN_TITLE_SCALE 2
#define PLAN_TEXT_SCALE 1
#define PLAN_BAR_X_OFFSET 30
#define PLAN_BAR_W 54
#define PLAN_BAR_H 8
#define PLAN_PCT_X_OFFSET 100
#define PLAN_TIME_X_OFFSET 131

#define SHARE_BAR_W 68
#define SHARE_TEXT_SCALE 2
#define FOOTER_SCALE 2

void dashboard_render(const DashboardDataV1* data) {
    writeSerial("Dashboard: rendering start", true);

    uint8_t* bwRow = staticRowBuffer;
    // Red plane row buffer: use a separate static buffer
    static uint8_t redRowBuf[680];
    uint8_t* redRow = redRowBuf;

    // Init display hardware
    touchSuspendForEpdRefresh();
    if (displayPowerState) { pwrmgm(false); delay(50); }
    pwrmgm(true);
    bbepInitIO(&bbep,
        globalConfig.displays[0].dc_pin,
        globalConfig.displays[0].reset_pin,
        globalConfig.displays[0].busy_pin,
        globalConfig.displays[0].cs_pin,
        globalConfig.displays[0].data_pin,
        globalConfig.displays[0].clk_pin,
        8000000);
    bbepWakeUp(&bbep);
    bbepSendCMDSequence(&bbep, bbep.pInitFull);
    bbepSetAddrWindow(&bbep, 0, 0, DW, DH);
    bbepStartWrite(&bbep, PLANE_0);

    // Pre-format strings
    char totalStr[16], inputStr[16], outputStr[16], cacheStr[16];
    dashboard_format_tokens(totalStr, data->total_tokens);
    dashboard_format_tokens(inputStr, data->input_tokens);
    dashboard_format_tokens(outputStr, data->output_tokens);
    dashboard_format_tokens(cacheStr, data->cache_tokens);

    char glmTitle[24];
    if (data->glm_level[0]) snprintf(glmTitle, sizeof(glmTitle), "GLM %s", data->glm_level);
    else snprintf(glmTitle, sizeof(glmTitle), "GLM");

    bool glm5hAlert = data->glm_5h_percent >= 80;
    bool glmWkAlert = data->glm_week_percent >= 80;
    bool gpt5hAlert = data->gpt_5h_percent >= 80;
    bool gptWkAlert = data->gpt_week_percent >= 80;

    char glm5hPct[8], glmWkPct[8], gpt5hPct[8], gptWkPct[8];
    snprintf(glm5hPct, sizeof(glm5hPct), "%u%%", data->glm_5h_percent);
    snprintf(glmWkPct, sizeof(glmWkPct), "%u%%", data->glm_week_percent);
    snprintf(gpt5hPct, sizeof(gpt5hPct), "%u%%", data->gpt_5h_percent);
    snprintf(gptWkPct, sizeof(gptWkPct), "%u%%", data->gpt_week_percent);

    char modelStrs[4][16];
    char modelTokenStrs[4][16];
    char modelShareStrs[4][8];
    char modelCallStrs[4][8];
    for (uint8_t i = 0; i < data->row_count && i < 4; i++) {
        // Truncate model name to fit
        const char* src = data->models[i].model;
        uint8_t slen = 0;
        while (src[slen] && slen < 12) { modelStrs[i][slen] = src[slen]; slen++; }
        modelStrs[i][slen] = '\0';
        dashboard_format_tokens(modelTokenStrs[i], data->models[i].total_tokens);
        dashboard_format_percent(modelShareStrs[i], data->models[i].share_bp);
        snprintf(modelCallStrs[i], sizeof(modelCallStrs[i]), "%u", (unsigned)data->models[i].calls);
    }

    // --- Row-by-row rendering ---
    for (uint16_t y = 0; y < DH; y++) {
        memset(bwRow, 0xFF, PITCH);
        memset(redRow, 0x00, PITCH);

        // == Overview box ==
        drawRRect(bwRow, y, OX1, OY1, OX2, OY2);
        // Vertical dashed separators between 4 columns
        uint16_t colX[4] = {
            OX1,
            OX1 + OVERVIEW_COL0_W,
            OX1 + OVERVIEW_COL0_W + OVERVIEW_COL_W,
            OX1 + OVERVIEW_COL0_W + OVERVIEW_COL_W * 2
        };
        uint16_t colW[4] = {
            OVERVIEW_COL0_W,
            OVERVIEW_COL_W,
            OVERVIEW_COL_W,
            OVERVIEW_COL_W
        };
        for (uint8_t ci = 1; ci < 4; ci++) {
            drawDashedVLine(bwRow, y, colX[ci], OY1+8, OY2-8);
        }
        // Labels
        const char* overviewLabels[4] = {"TOTAL TOKEN", "INPUT", "OUTPUT", "CACHE"};
        for (uint8_t ci = 0; ci < 4; ci++) {
            uint16_t labelX = colX[ci] + (colW[ci] - textWidth(overviewLabels[ci], OVERVIEW_TITLE_SCALE)) / 2;
            drawTextRow(bwRow, y, labelX, OY1+8, overviewLabels[ci], OVERVIEW_TITLE_SCALE);
        }
        // Values
        uint16_t totalValX = colX[0] + (colW[0] - textWidth(totalStr, OVERVIEW_TOTAL_VALUE_SCALE)) / 2;
        uint16_t totalValY = OY1 + 42;
        drawTextRow(bwRow, y, totalValX, totalValY, totalStr, OVERVIEW_TOTAL_VALUE_SCALE);

        const char* overviewValues[3] = {inputStr, outputStr, cacheStr};
        for (uint8_t ci = 0; ci < 3; ci++) {
            uint16_t valueX = colX[ci + 1] + (colW[ci + 1] - textWidth(overviewValues[ci], OVERVIEW_VALUE_SCALE)) / 2;
            drawTextRow(bwRow, y, valueX, OY1+44, overviewValues[ci], OVERVIEW_VALUE_SCALE);
        }

        // == GLM Plan card ==
        drawRRect(bwRow, y, GLM_X1, GLM_Y1, GLM_X2, GLM_Y2);
        drawTextRow(bwRow, y, GLM_X1+8, GLM_Y1+8, glmTitle, PLAN_TITLE_SCALE);
        // 5H row
        drawTextRow(bwRow, y, GLM_X1+8, GLM_Y1+28, "5H", PLAN_TEXT_SCALE);
        drawProgressBar(bwRow, redRow, y, GLM_X1+PLAN_BAR_X_OFFSET, PLAN_BAR_W, PLAN_BAR_H, GLM_Y1+31, data->glm_5h_percent, glm5hAlert);
        if (glm5hAlert) drawTextRowRed(redRow, y, GLM_X1+PLAN_PCT_X_OFFSET, GLM_Y1+27, glm5hPct, PLAN_TEXT_SCALE);
        else drawTextRow(bwRow, y, GLM_X1+PLAN_PCT_X_OFFSET, GLM_Y1+27, glm5hPct, PLAN_TEXT_SCALE);
        drawTextRow(bwRow, y, GLM_X1+PLAN_TIME_X_OFFSET, GLM_Y1+27, data->glm_5h_label, PLAN_TEXT_SCALE);
        // Week row
        drawTextRow(bwRow, y, GLM_X1+8, GLM_Y1+48, "1W", PLAN_TEXT_SCALE);
        drawProgressBar(bwRow, redRow, y, GLM_X1+PLAN_BAR_X_OFFSET, PLAN_BAR_W, PLAN_BAR_H, GLM_Y1+51, data->glm_week_percent, glmWkAlert);
        if (glmWkAlert) drawTextRowRed(redRow, y, GLM_X1+PLAN_PCT_X_OFFSET, GLM_Y1+47, glmWkPct, PLAN_TEXT_SCALE);
        else drawTextRow(bwRow, y, GLM_X1+PLAN_PCT_X_OFFSET, GLM_Y1+47, glmWkPct, PLAN_TEXT_SCALE);
        drawTextRow(bwRow, y, GLM_X1+PLAN_TIME_X_OFFSET, GLM_Y1+47, data->glm_week_label, PLAN_TEXT_SCALE);

        // == GPT Plan card ==
        drawRRect(bwRow, y, GPT_X1, GPT_Y1, GPT_X2, GPT_Y2);
        drawTextRow(bwRow, y, GPT_X1+8, GPT_Y1+8, "GPT Plus", PLAN_TITLE_SCALE);
        drawTextRow(bwRow, y, GPT_X1+8, GPT_Y1+28, "5H", PLAN_TEXT_SCALE);
        drawProgressBar(bwRow, redRow, y, GPT_X1+PLAN_BAR_X_OFFSET, PLAN_BAR_W, PLAN_BAR_H, GPT_Y1+31, data->gpt_5h_percent, gpt5hAlert);
        if (gpt5hAlert) drawTextRowRed(redRow, y, GPT_X1+PLAN_PCT_X_OFFSET, GPT_Y1+27, gpt5hPct, PLAN_TEXT_SCALE);
        else drawTextRow(bwRow, y, GPT_X1+PLAN_PCT_X_OFFSET, GPT_Y1+27, gpt5hPct, PLAN_TEXT_SCALE);
        drawTextRow(bwRow, y, GPT_X1+PLAN_TIME_X_OFFSET, GPT_Y1+27, data->gpt_5h_label, PLAN_TEXT_SCALE);
        drawTextRow(bwRow, y, GPT_X1+8, GPT_Y1+48, "1W", PLAN_TEXT_SCALE);
        drawProgressBar(bwRow, redRow, y, GPT_X1+PLAN_BAR_X_OFFSET, PLAN_BAR_W, PLAN_BAR_H, GPT_Y1+51, data->gpt_week_percent, gptWkAlert);
        if (gptWkAlert) drawTextRowRed(redRow, y, GPT_X1+PLAN_PCT_X_OFFSET, GPT_Y1+47, gptWkPct, PLAN_TEXT_SCALE);
        else drawTextRow(bwRow, y, GPT_X1+PLAN_PCT_X_OFFSET, GPT_Y1+47, gptWkPct, PLAN_TEXT_SCALE);
        drawTextRow(bwRow, y, GPT_X1+PLAN_TIME_X_OFFSET, GPT_Y1+47, data->gpt_week_label, PLAN_TEXT_SCALE);

        // == Model table ==
        drawRRect(bwRow, y, TBL_X1, TBL_Y1, TBL_X2, TBL_Y2);
        uint16_t hdrBot = TBL_Y1 + 18;
        uint16_t tblCols[5] = {TBL_X1, TBL_X1+92, TBL_X1+148, TBL_X1+234, TBL_X2};
        // Header
        if (y >= TBL_Y1 && y <= hdrBot) {
            drawTextRow(bwRow, y, tblCols[0]+6, TBL_Y1+4, "MODEL", SHARE_TEXT_SCALE);
            drawTextRow(bwRow, y, tblCols[1]+6, TBL_Y1+4, "CALLS", SHARE_TEXT_SCALE);
            drawTextRow(bwRow, y, tblCols[2]+6, TBL_Y1+4, "TOKEN", SHARE_TEXT_SCALE);
            drawTextRow(bwRow, y, tblCols[3]+6, TBL_Y1+4, "SHARE", SHARE_TEXT_SCALE);
        }
        drawHLine(bwRow, y, TBL_X1, TBL_X2, hdrBot);
        // Vertical header separators
        for (uint8_t ci = 1; ci < 4; ci++) {
            drawVLine(bwRow, y, tblCols[ci], hdrBot, TBL_Y2);
        }
        // Data rows (20px each)
        for (uint8_t ri = 0; ri < data->row_count && ri < 4; ri++) {
            uint16_t rowTop = hdrBot + ri * 20;
            uint16_t rowCenter = rowTop + 10;
            if (y > hdrBot && y < TBL_Y2) {
                drawTextRow(bwRow, y, tblCols[0]+6, rowCenter-5, modelStrs[ri], PLAN_TEXT_SCALE);
                drawTextRow(bwRow, y, tblCols[1]+28, rowCenter-5, modelCallStrs[ri], PLAN_TEXT_SCALE);
                drawTextRow(bwRow, y, tblCols[2]+6, rowCenter-5, modelTokenStrs[ri], PLAN_TEXT_SCALE);
                // Share bar + text
                uint16_t barX = tblCols[3]+6;
                uint16_t barW = SHARE_BAR_W;
                drawRect(bwRow, y, barX, rowCenter-4, barX+barW, rowCenter+4);
                uint8_t pct = (uint8_t)(data->models[ri].share_bp / 100);
                int fw = (int)((barW-2) * pct / 100);
                if (fw > barW-2) fw = barW-2;
                if (y > rowCenter-4 && y < rowCenter+4 && fw > 0) {
                    for (int fi = 0; fi < fw; fi++) setPixelBlack(bwRow, barX+1+fi);
                }
                drawTextRow(bwRow, y, barX + barW + 8, rowCenter-5, modelShareStrs[ri], SHARE_TEXT_SCALE);
            }
            // Row separator
            if (ri < 3 && data->row_count > ri + 1) {
                drawHLine(bwRow, y, TBL_X1, TBL_X2, rowTop + 20);
            }
        }

        // == Footer ==
        char footerStr[32];
        snprintf(footerStr, sizeof(footerStr), "LAST: %s", data->last_refresh);
        drawTextRow(bwRow, y, 12, 278, footerStr, FOOTER_SCALE);

        // Write BW plane row
        bbepWriteData(&bbep, bwRow, PITCH);
    }

    // --- Red plane ---
    bbepSetAddrWindow(&bbep, 0, 0, DW, DH);
    bbepStartWrite(&bbep, PLANE_1);
    // Red plane: re-render only red-marked rows
    for (uint16_t y = 0; y < DH; y++) {
        memset(redRow, 0x00, PITCH);

        // GLM plan progress bars
        if (glm5hAlert) {
            if (y > GLM_Y1+31 && y < GLM_Y1+31+PLAN_BAR_H) {
                int fw = (int)((PLAN_BAR_W - 2) * data->glm_5h_percent / 100);
                for (int i = 0; i < fw; i++) setPixelRed(redRow, GLM_X1+31+i);
            }
            drawTextRowRed(redRow, y, GLM_X1+PLAN_PCT_X_OFFSET, GLM_Y1+27, glm5hPct, PLAN_TEXT_SCALE);
        }
        if (glmWkAlert) {
            if (y > GLM_Y1+51 && y < GLM_Y1+51+PLAN_BAR_H) {
                int fw = (int)((PLAN_BAR_W - 2) * data->glm_week_percent / 100);
                for (int i = 0; i < fw; i++) setPixelRed(redRow, GLM_X1+31+i);
            }
            drawTextRowRed(redRow, y, GLM_X1+PLAN_PCT_X_OFFSET, GLM_Y1+47, glmWkPct, PLAN_TEXT_SCALE);
        }
        // GPT plan progress bars
        if (gpt5hAlert) {
            if (y > GPT_Y1+31 && y < GPT_Y1+31+PLAN_BAR_H) {
                int fw = (int)((PLAN_BAR_W - 2) * data->gpt_5h_percent / 100);
                for (int i = 0; i < fw; i++) setPixelRed(redRow, GPT_X1+31+i);
            }
            drawTextRowRed(redRow, y, GPT_X1+PLAN_PCT_X_OFFSET, GPT_Y1+27, gpt5hPct, PLAN_TEXT_SCALE);
        }
        if (gptWkAlert) {
            if (y > GPT_Y1+51 && y < GPT_Y1+51+PLAN_BAR_H) {
                int fw = (int)((PLAN_BAR_W - 2) * data->gpt_week_percent / 100);
                for (int i = 0; i < fw; i++) setPixelRed(redRow, GPT_X1+31+i);
            }
            drawTextRowRed(redRow, y, GPT_X1+PLAN_PCT_X_OFFSET, GPT_Y1+47, gptWkPct, PLAN_TEXT_SCALE);
        }

        bbepWriteData(&bbep, redRow, PITCH);
    }

    // --- Refresh ---
    writeSerial("Dashboard: triggering full refresh", true);
    bbepRefresh(&bbep, REFRESH_FULL);
    bool success = waitforrefresh(60);
    bbepSleep(&bbep, 1);
    epdRefreshInProgress = false;
    pwrmgm(false);
    touchResumeAfterEpdRefresh();
#ifdef TARGET_ESP32
    esp32_restart_ble_advertising();
#endif
    writeSerial(success ? "Dashboard: refresh OK" : "Dashboard: refresh TIMEOUT", true);
}
