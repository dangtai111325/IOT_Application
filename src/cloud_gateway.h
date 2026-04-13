#pragma once

#include "app_state.h"

namespace receiver
{

// ============================================================
//  Giao tiếp CoreIoT / MQTT Gateway
//  Module này lo toàn bộ đường đi cloud: connect, telemetry và RPC.
// ============================================================

String cloudStatusText();
void publishRpcResponse(const char *deviceName, uint32_t requestId, bool success, const char *message, float threshold = NAN);
bool publishGatewayDisconnect(const char *deviceName);
void maintainMqtt();

} // namespace receiver
