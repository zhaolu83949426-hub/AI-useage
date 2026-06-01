#include "app_state.h"

NimBLEServer* g_bleServer = nullptr;
NimBLEService* g_bleService = nullptr;
NimBLECharacteristic* g_bleCharacteristic = nullptr;
bool g_bleConnected = false;
bool g_advertisingRestartPending = false;
bool g_rebootFlag = false;
bool g_connectionRequested = false;
uint8_t g_mloopCounter = 0;
uint8_t g_msdPayload[16] = {};
PacketQueue<app::kCommandQueueSize, app::kMaxPacketSize> g_commandQueue;
PacketQueue<app::kResponseQueueSize, app::kMaxPacketSize> g_responseQueue;
DirectWriteState g_directWriteState;
DashboardProtocolContext g_dashboardContext;
DashboardRenderState g_dashboardRenderState;
uint8_t g_requestedRefreshMode = app::kRefreshModeFull;
DeferredJob g_deferredJob = DeferredJob::None;
uint32_t g_deferredJobReadyAtMs = 0;
