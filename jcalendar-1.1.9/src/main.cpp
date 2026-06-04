#include <Arduino.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <sys/time.h>

#include "app_config.h"
#include "app_state.h"
#include "battery.h"
#include "ble_service.h"
#include "display_service.h"
#include "protocol.h"

// 时间同步模式：电池供电时自动启用
static bool g_timeSyncMode = false;
// 渲染完成后应进入深度睡眠
static bool g_sleepAfterRender = false;
// 唤醒窗口截止时间 (ms)
static uint32_t g_wakeDeadlineMs = 0;
// RTC 数据：深睡前标记是否处于时间同步模式（跨深睡眠保留）
RTC_DATA_ATTR static bool g_rtcTimeSyncMode = false;
// 上次 RTC 同步的时间戳（跨深睡眠保留）
RTC_DATA_ATTR static time_t g_rtcLastSyncTime = 0;

static bool is_battery_powered() {
    int voltage_mv = readBatteryVoltage();
    // 电池电压 < 4.2V 视为电池供电（非 USB 充电）
    return voltage_mv < app::kBatteryThresholdMv;
}

// 用推送数据中的 sync_hour/sync_minute/sync_second 同步本地 RTC
static void sync_rtc_from_dashboard(const DashboardDataV1& data) {
    if (data.sync_hour > 23 || data.sync_minute > 59 || data.sync_second > 59) {
        return;
    }
    time_t now = time(nullptr);
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);
    tm_info.tm_hour = data.sync_hour;
    tm_info.tm_min = data.sync_minute;
    tm_info.tm_sec = data.sync_second;
    time_t corrected = mktime(&tm_info);
    struct timeval tv = { .tv_sec = corrected, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    g_rtcLastSyncTime = time(nullptr);
    Serial.printf("RTC synced from dashboard: %02d:%02d:%02d\n",
                  data.sync_hour, data.sync_minute, data.sync_second);

    // 首次同步后激活时间同步省电模式
    if (!g_timeSyncMode && is_battery_powered()) {
        g_timeSyncMode = true;
        g_wakeDeadlineMs = millis() + app::kWakeWindowSec * 1000;
        g_sleepAfterRender = false;
        Serial.println("RTC synced, time sync mode activated");
    }
}

// 计算到下一个5分钟整点的秒数
static uint32_t seconds_to_next_wake() {
    time_t now = time(nullptr);
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);

    uint8_t current_hour = tm_info.tm_hour;
    uint8_t current_min = tm_info.tm_min;
    uint8_t current_sec = tm_info.tm_sec;

    // 夜间模式：睡到 kNightEndHour，但限制单次最长 kMaxDeepSleepSec
    if (current_hour >= app::kNightStartHour && current_hour < app::kNightEndHour) {
        uint32_t secs_to_morning = 0;
        struct tm target = tm_info;
        target.tm_hour = app::kNightEndHour;
        target.tm_min = 0;
        target.tm_sec = 0;
        secs_to_morning = static_cast<uint32_t>(difftime(mktime(&target), now));
        if (secs_to_morning == 0) secs_to_morning = 1;
        if (secs_to_morning > app::kMaxDeepSleepSec) {
            Serial.printf("Night mode: capped sleep %lus -> %lus\n",
                           static_cast<unsigned long>(secs_to_morning),
                           static_cast<unsigned long>(app::kMaxDeepSleepSec));
            secs_to_morning = app::kMaxDeepSleepSec;
        } else {
            Serial.printf("Night mode: sleeping until %02d:00 (%lus)\n",
                           app::kNightEndHour, static_cast<unsigned long>(secs_to_morning));
        }
        return secs_to_morning;
    }

    // 下一个5分钟整点
    uint8_t next_5 = ((current_min / app::kWakeIntervalMin) + 1) * app::kWakeIntervalMin;
    uint32_t secs = 0;
    if (next_5 >= 60) {
        secs = (60 - current_min - 1) * 60 + (60 - current_sec);
    } else {
        secs = (next_5 - current_min) * 60 - current_sec;
    }
    if (secs == 0) secs = app::kWakeIntervalMin * 60;
    return secs;
}

static void enter_deep_sleep(uint32_t sleep_sec) {
    Serial.printf("Entering deep sleep for %lus\n", static_cast<unsigned long>(sleep_sec));
    Serial.flush();
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleep_sec) * 1000000ULL);
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    delay(100);

    bool timer_wakeup = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER);

    // 定时唤醒 + 之前处于时间同步模式 + 仍在夜间 → 跳过完整初始化直接回睡
    if (timer_wakeup && g_rtcTimeSyncMode) {
        time_t now = time(nullptr);
        struct tm tm_info = {};
        localtime_r(&now, &tm_info);
        if (tm_info.tm_hour >= app::kNightStartHour && tm_info.tm_hour < app::kNightEndHour) {
            Serial.println("Night mode: continuing sleep (fast path)");
            uint32_t sleep_sec = seconds_to_next_wake();
            enter_deep_sleep(sleep_sec);
            return;
        }
    }

    SPI.begin(SPI_SCK, -1, SPI_MOSI, SPI_CS);
    init_protocol_state();
    init_ble_service();
    init_display_service();

    // 电池供电时启用时间同步省电模式
    g_timeSyncMode = is_battery_powered();

    if (g_timeSyncMode) {
        time_t now = time(nullptr);
        struct tm tm_info = {};
        localtime_r(&now, &tm_info);

        // RTC 未初始化（年份 < 2024）或超过 3 天未同步 → 不进入省电模式，保持唤醒等待推送
        bool rtc_valid = (tm_info.tm_year >= (2024 - 1900));
        bool sync_recent = (g_rtcLastSyncTime > 0 && (now - g_rtcLastSyncTime) <= (3 * 86400));
        if (!rtc_valid || !sync_recent) {
            g_timeSyncMode = false;
            Serial.printf("RTC not ready (valid=%d, sync_age=%lus), staying awake for time sync\n",
                           rtc_valid,
                           g_rtcLastSyncTime > 0 ? static_cast<unsigned long>(now - g_rtcLastSyncTime) : 0);
        }
    }

    if (g_timeSyncMode) {
        Serial.println("Time sync mode enabled (battery power saving)");

        time_t now = time(nullptr);
        struct tm tm_info = {};
        localtime_r(&now, &tm_info);

        // 夜间模式：初始化后直接深度睡眠到早上
        if (tm_info.tm_hour >= app::kNightStartHour && tm_info.tm_hour < app::kNightEndHour) {
            Serial.println("Night mode: sleeping until morning");
            g_rtcTimeSyncMode = true;
            uint32_t sleep_sec = seconds_to_next_wake();
            enter_deep_sleep(sleep_sec);
            return;
        }

        // 设置唤醒窗口截止时间
        g_wakeDeadlineMs = millis() + app::kWakeWindowSec * 1000;
        g_sleepAfterRender = false;
    }
    g_rtcTimeSyncMode = g_timeSyncMode;
}

static void process_one_command() {
    PacketQueue<app::kCommandQueueSize, app::kMaxPacketSize>::Item item;
    if (!g_commandQueue.pop(item)) {
        return;
    }
    process_command_packet(item.data, item.len);
}

void loop() {
    process_one_command();
    process_ble_responses();
    process_deferred_job();
    process_ble_responses();
    process_ble_advertising_restart();

    // 时间同步模式：渲染完成后进入深度睡眠
    if (g_timeSyncMode && g_sleepAfterRender) {
        // 同步 RTC（从最近一次推送数据）
        uint32_t sleep_sec = seconds_to_next_wake();
        enter_deep_sleep(sleep_sec);
        return;
    }

    // 时间同步模式：唤醒窗口超时，未收到数据也回睡眠
    if (g_timeSyncMode && g_wakeDeadlineMs > 0 && millis() > g_wakeDeadlineMs) {
        Serial.println("Wake window expired, returning to sleep");
        uint32_t sleep_sec = seconds_to_next_wake();
        enter_deep_sleep(sleep_sec);
        return;
    }

    delay(1);
}

// 供 protocol.cpp 调用：渲染完成后标记睡眠
void mark_sleep_after_render() {
    if (g_timeSyncMode) {
        g_sleepAfterRender = true;
    }
}

// 供 protocol.cpp 调用：推送数据到达后同步 RTC
void notify_dashboard_data(const DashboardDataV1& data) {
    sync_rtc_from_dashboard(data);
}
