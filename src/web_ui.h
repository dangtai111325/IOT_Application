#pragma once

#include <ESPAsyncWebServer.h>

namespace sender
{

void startWebServer();
void pushSnapshot(AsyncWebSocketClient *target = nullptr);

} // namespace sender
