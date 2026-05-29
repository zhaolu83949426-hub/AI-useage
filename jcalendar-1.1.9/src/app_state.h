#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "app_config.h"
#include "dashboard_protocol.h"
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

extern NimBLEServer* g_bleServer;
extern NimBLEService* g_bleService;
extern NimBLECharacteristic* g_bleCharacteristic;
extern bool g_bleConnected;
extern bool g_advertisingRestartPending;
extern bool g_rebootFlag;
extern bool g_connectionRequested;
extern uint8_t g_mloopCounter;
extern uint8_t g_msdPayload[16];
extern PacketQueue<app::kCommandQueueSize, app::kMaxPacketSize> g_commandQueue;
extern PacketQueue<app::kResponseQueueSize, app::kMaxPacketSize> g_responseQueue;
extern DirectWriteState g_directWriteState;
extern DashboardProtocolContext g_dashboardContext;
extern DeferredJob g_deferredJob;
extern uint32_t g_deferredJobReadyAtMs;
