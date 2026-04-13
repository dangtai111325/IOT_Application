#pragma once

#include "app_state.h"

namespace receiver
{

// ============================================================
//  Giao diện web và WebSocket
//  Module này phục vụ UI, nhận action và đẩy snapshot cho trình duyệt.
// ============================================================

void startWebServer();
void pushSnapshot(AsyncWebSocketClient *target = nullptr);

} // namespace receiver
