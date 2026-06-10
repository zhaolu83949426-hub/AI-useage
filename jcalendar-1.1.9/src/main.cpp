#include <Arduino.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <sys/time.h>

#include "app_config.h"
#include "app_state.h"
#include "ble_service.h"
#include "display_service.h"
#include "protocol.h"

// 时间同步模式：RTC 就绪后按固定周期唤醒并等待主机推送
static bool g_timeSyncMode = false;
// 渲染完成后应进入深度睡眠
static bool g_sleepAfterRender = false;
// 唤醒窗口截止时间 (ms)
static uint32_t g_wakeDeadlineMs = 0;
// 单击按钮后保持唤醒，等待下一次数据推送成功后再恢复循环睡眠
static bool g_waitNextPushAfterButton = false;
static volatile bool g_buttonPressed = false;
static uint32_t g_lastButtonPressMs = 0;
// RTC 数据：深睡前标记是否处于时间同步模式（跨深睡眠保留）
RTC_DATA_ATTR static bool g_rtcTimeSyncMode = false;
// 上次 RTC 同步的时间戳（跨深睡眠保留）
RTC_DATA_ATTR static time_t g_rtcLastSyncTime = 0;
// 按钮唤醒后保留等待推送状态
RTC_DATA_ATTR static bool g_rtcWaitNextPushAfterButton = false;

static void IRAM_ATTR handle_button_interrupt() {
    g_buttonPressed = true;
}

static void reset_wake_timeout_state() {
    g_wakeTimeoutCount = 0;
    g_wakeCyclePaused = false;
}

static bool has_active_push_session() {
    return g_bleConnected ||
        g_deferredJob != DeferredJob::None ||
        g_directWriteState.active ||
        !g_commandQueue.empty() ||
        !g_responseQueue.empty();
}

static void enter_button_resume_sleep() {
    Serial.println("Entering deep sleep until button wakeup");
    Serial.flush();
    esp_sleep_enable_ext0_wakeup(KEY_M, 0);
    esp_deep_sleep_start();
}

static void resume_periodic_wake_cycle(const char* reason) {
    reset_wake_timeout_state();
    g_timeSyncMode = true;
    g_sleepAfterRender = false;
    g_wakeDeadlineMs = millis() + app::kWakeWindowSec * 1000;
    g_waitNextPushAfterButton = false;
    g_rtcWaitNextPushAfterButton = false;
    g_rtcTimeSyncMode = true;
    Serial.printf("Periodic wake cycle resumed: %s\n", reason);
}

static void pause_periodic_wake_cycle() {
    g_timeSyncMode = false;
    g_sleepAfterRender = false;
    g_wakeDeadlineMs = 0;
    g_waitNextPushAfterButton = false;
    g_rtcWaitNextPushAfterButton = false;
    g_rtcTimeSyncMode = false;
    g_wakeCyclePaused = true;
    g_wakeTimeoutCount = 0;
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

    // 首次同步后激活周期唤醒模式
    if (!g_timeSyncMode && !g_waitNextPushAfterButton) {
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
    esp_sleep_enable_ext0_wakeup(KEY_M, 0);
    esp_deep_sleep_start();
}

static void activate_button_wait_mode(const char* reason) {
    g_timeSyncMode = false;
    g_sleepAfterRender = false;
    g_wakeDeadlineMs = 0;
    g_waitNextPushAfterButton = true;
    g_rtcWaitNextPushAfterButton = true;
    g_rtcTimeSyncMode = false;
    g_forceFullRefresh = true;
    Serial.printf("Button wait mode activated: %s\n", reason);
}

static void setup_button() {
    pinMode(KEY_M, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(KEY_M), handle_button_interrupt, FALLING);
}

static void process_button_press() {
    if (!g_buttonPressed) {
        return;
    }
    g_buttonPressed = false;

    uint32_t now_ms = millis();
    if (now_ms - g_lastButtonPressMs < app::kButtonDebounceMs) {
        return;
    }
    g_lastButtonPressMs = now_ms;
    if (g_wakeCyclePaused) {
        resume_periodic_wake_cycle("button");
        return;
    }
    activate_button_wait_mode("single click");
}

static bool should_continue_night_sleep(bool timer_wakeup) {
    if (!timer_wakeup || !g_rtcTimeSyncMode || g_rtcWaitNextPushAfterButton) {
        return false;
    }

    time_t now = time(nullptr);
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);
    return tm_info.tm_hour >= app::kNightStartHour && tm_info.tm_hour < app::kNightEndHour;
}

static void configure_power_saving_mode() {
    if (g_wakeCyclePaused) {
        g_timeSyncMode = false;
        return;
    }

    g_timeSyncMode = !g_waitNextPushAfterButton;
    if (!g_timeSyncMode) {
        return;
    }

    time_t now = time(nullptr);
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);

    // RTC 未初始化或同步过期时保持唤醒，等待主机推送重新校时。
    bool rtc_valid = (tm_info.tm_year >= (2024 - 1900));
    bool sync_recent = (g_rtcLastSyncTime > 0 && (now - g_rtcLastSyncTime) <= (3 * 86400));
    if (rtc_valid && sync_recent) {
        return;
    }

    g_timeSyncMode = false;
    Serial.printf("RTC not ready (valid=%d, sync_age=%lus), staying awake for time sync\n",
                  rtc_valid,
                  g_rtcLastSyncTime > 0 ? static_cast<unsigned long>(now - g_rtcLastSyncTime) : 0);
}

static bool enter_night_sleep_if_needed() {
    if (!g_timeSyncMode) {
        return false;
    }

    Serial.println("Time sync mode enabled (battery power saving)");
    time_t now = time(nullptr);
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);

    if (tm_info.tm_hour < app::kNightStartHour || tm_info.tm_hour >= app::kNightEndHour) {
        g_wakeDeadlineMs = millis() + app::kWakeWindowSec * 1000;
        g_sleepAfterRender = false;
        return false;
    }

    Serial.println("Night mode: sleeping until morning");
    g_rtcTimeSyncMode = true;
    uint32_t sleep_sec = seconds_to_next_wake();
    enter_deep_sleep(sleep_sec);
    return true;
}

static void handle_wake_window_expired() {
    // 一旦进入 BLE 会话或已开始渲染，就必须等本轮处理完成后再睡。
    if (has_active_push_session()) {
        return;
    }

    g_wakeTimeoutCount = static_cast<uint8_t>(g_wakeTimeoutCount + 1);
    Serial.printf("Wake window expired without BLE session (%u/%u)\n",
                  g_wakeTimeoutCount,
                  app::kWakeTimeoutLimit);
    if (g_wakeTimeoutCount >= app::kWakeTimeoutLimit) {
        Serial.println("Wake retry limit reached, sleeping until button press");
        pause_periodic_wake_cycle();
        enter_button_resume_sleep();
        return;
    }

    uint32_t sleep_sec = seconds_to_next_wake();
    enter_deep_sleep(sleep_sec);
}

void setup() {
    Serial.begin(115200);
    delay(100);

    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    bool timer_wakeup = (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER);
    bool button_wakeup = (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0);

    // 定时唤醒 + 之前处于时间同步模式 + 仍在夜间 → 跳过完整初始化直接回睡
    if (should_continue_night_sleep(timer_wakeup)) {
        Serial.println("Night mode: continuing sleep (fast path)");
        uint32_t sleep_sec = seconds_to_next_wake();
        enter_deep_sleep(sleep_sec);
        return;
    }

    SPI.begin(SPI_SCK, -1, SPI_MOSI, SPI_CS);
    setup_button();
    init_protocol_state();
    init_ble_service();

    g_waitNextPushAfterButton = g_rtcWaitNextPushAfterButton;
    if (button_wakeup) {
        if (g_wakeCyclePaused) {
            resume_periodic_wake_cycle("button wakeup");
            return;
        }
        activate_button_wait_mode("wakeup");
    }

    // RTC 就绪时启用时间同步周期唤醒模式
    configure_power_saving_mode();
    if (enter_night_sleep_if_needed()) {
        return;
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
    process_button_press();
    process_one_command();
    process_ble_responses();
    process_deferred_job();
    process_ble_responses();
    process_ble_advertising_restart();

    // 时间同步模式：渲染完成后进入深度睡眠
    if (g_timeSyncMode && g_sleepAfterRender) {
        // 等待最终刷新通知送出，避免深睡过早切断 BLE notify。
        delay(app::kBleNotifyDrainDelayMs);
        uint32_t sleep_sec = seconds_to_next_wake();
        enter_deep_sleep(sleep_sec);
        return;
    }

    // 时间同步模式：唤醒窗口超时，未收到数据也回睡眠
    if (g_timeSyncMode && g_wakeDeadlineMs > 0 && millis() > g_wakeDeadlineMs) {
        handle_wake_window_expired();
        return;
    }

    delay(1);
}

// 供 protocol.cpp 调用：渲染完成后标记睡眠
void mark_sleep_after_render() {
    reset_wake_timeout_state();
    if (g_waitNextPushAfterButton) {
        g_waitNextPushAfterButton = false;
        g_rtcWaitNextPushAfterButton = false;
        g_timeSyncMode = true;
        g_rtcTimeSyncMode = g_timeSyncMode;
    }
    g_forceFullRefresh = false;
    if (g_timeSyncMode) {
        g_sleepAfterRender = true;
    }
}

// 供 protocol.cpp 调用：无数据变化时跳过刷屏并尽快回到深睡眠。
void mark_sleep_without_render() {
    if (g_timeSyncMode) {
        reset_wake_timeout_state();
        g_rtcTimeSyncMode = true;
        g_sleepAfterRender = true;
    }
}

// 供 protocol.cpp 调用：推送数据到达后同步 RTC
void notify_dashboard_data(const DashboardDataV1& data) {
    sync_rtc_from_dashboard(data);
}

// 供 protocol.cpp 调用：无变化命令只同步时间，不触发刷屏。
void notify_dashboard_time(uint8_t hour, uint8_t minute, uint8_t second) {
    DashboardDataV1 data = {};
    data.sync_hour = hour;
    data.sync_minute = minute;
    data.sync_second = second;
    sync_rtc_from_dashboard(data);
}
