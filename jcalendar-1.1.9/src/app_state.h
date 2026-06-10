#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "app_config.h"
#include "dashboard_protocol.h"
#include "dashboard_renderer.h"
#include "ring_queue.h"

enum class DeferredJob : uint8_t {
    None,
    DirectWriteRefresh,
    DashboardRender,
};

enum class DirectWritePhase : uint8_t {
    Idle,
    BlackPlane,
    RedPlane,
};

struct DirectWriteState {
    bool active = false;
    DirectWritePhase phase = DirectWritePhase::Idle;
    size_t received_bytes = 0;
    uint8_t black_plane[app::kPlaneSize] = {};
    uint8_t red_plane[app::kPlaneSize] = {};
};

struct DashboardRenderState {
    bool partial_baseline_ready = false;
    uint8_t fast_refresh_count = 0;
    uint8_t last_battery_percent = 0;
    DashboardDataV1 last_data = {};
};

extern NimBLEServer* g_bleServer;
extern NimBLEService* g_bleService;
extern NimBLECharacteristic* g_bleCharacteristic;
extern bool g_bleConnected;
extern bool g_advertisingRestartPending;
extern bool g_rebootFlag;
extern bool g_connectionRequested;
extern bool g_forceFullRefresh;
extern uint8_t g_mloopCounter;
extern uint8_t g_msdPayload[16];
extern PacketQueue<app::kCommandQueueSize, app::kMaxPacketSize> g_commandQueue;
extern PacketQueue<app::kResponseQueueSize, app::kMaxPacketSize> g_responseQueue;
extern DirectWriteState g_directWriteState;
extern DashboardProtocolContext g_dashboardContext;
extern DashboardRenderState g_dashboardRenderState;
// 连续唤醒超时次数，跨深睡眠保留。
extern uint8_t g_wakeTimeoutCount;
// 达到超时上限后暂停周期唤醒，仅允许按键恢复。
extern bool g_wakeCyclePaused;
extern uint8_t g_requestedRefreshMode;
extern DeferredJob g_deferredJob;
extern uint32_t g_deferredJobReadyAtMs;
