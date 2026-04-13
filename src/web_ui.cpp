#include "web_ui.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

#include "app_state.h"
#include "storage.h"

namespace sender
{

namespace
{

void sendActionResult(AsyncWebSocketClient *client, bool success, const String &message)
{
    if (client == nullptr)
    {
        return;
    }

    JsonDocument doc;
    doc["type"] = "actionResult";
    doc["success"] = success;
    doc["message"] = message;

    String payload;
    serializeJson(doc, payload);
    client->text(payload);
}

void handleSaveConfig(AsyncWebSocketClient *client, JsonDocument &doc)
{
    copyToBuffer(String(doc["wifiSsid"] | ""), g_config.wifiSsid);
    copyToBuffer(String(doc["wifiPass"] | ""), g_config.wifiPass);
    copyToBuffer(String(doc["receiverIp"] | ""), g_config.receiverIp);
    g_config.receiverPort = doc["receiverPort"] | DEFAULT_RECEIVER_PORT;

    if (!doc["threshold"].isNull())
    {
        g_config.threshold = constrain(doc["threshold"].as<float>(), 0.0f, 1.0f);
    }

    sendActionResult(client, true, "Đã lưu cấu hình sender. Thiết bị sẽ khởi động lại.");
    saveConfig(true);
}

void handleSetThreshold(AsyncWebSocketClient *client, JsonDocument &doc)
{
    if (doc["threshold"].isNull())
    {
        sendActionResult(client, false, "Thiếu threshold.");
        return;
    }

    g_config.threshold = constrain(doc["threshold"].as<float>(), 0.0f, 1.0f);
    saveConfig(false);
    sendActionResult(client, true, "Đã cập nhật threshold cục bộ.");
    pushSnapshot();
}

void handleWsMessage(AsyncWebSocketClient *client, uint8_t *data, size_t len)
{
    JsonDocument doc;
    if (deserializeJson(doc, data, len) != DeserializationError::Ok)
    {
        sendActionResult(client, false, "JSON không hợp lệ.");
        return;
    }

    const String action = String(doc["action"] | "");
    if (action == "saveConfig")
    {
        handleSaveConfig(client, doc);
    }
    else if (action == "setThreshold")
    {
        handleSetThreshold(client, doc);
    }
    else
    {
        sendActionResult(client, false, "Action không được hỗ trợ.");
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    (void)server;
    (void)arg;

    if (type == WS_EVT_CONNECT)
    {
        g_wsConnected = true;
        pushSnapshot(client);
        return;
    }

    if (type == WS_EVT_DISCONNECT)
    {
        g_wsConnected = g_ws.count() > 0;
        return;
    }

    if (type == WS_EVT_DATA)
    {
        handleWsMessage(client, data, len);
    }
}

} // namespace

void pushSnapshot(AsyncWebSocketClient *target)
{
    JsonDocument doc;
    doc["type"] = "snapshot";
    doc["deviceId"] = g_deviceId;
    doc["deviceName"] = g_config.deviceName;
    doc["temperature"] = g_temperature;
    doc["humidity"] = g_humidity;
    doc["threshold"] = g_config.threshold;
    doc["aiScore"] = g_aiScore;
    doc["aiStatus"] = g_aiStatus;
    doc["latitude"] = g_latitude;
    doc["longitude"] = g_longitude;
    doc["receiverConnected"] = g_tcpClient.connected();
    doc["receiverStatus"] = g_receiverStatus;
    doc["receiverIp"] = g_config.receiverIp;
    doc["resolvedReceiverIp"] = resolvedReceiverIp();
    doc["receiverPort"] = g_config.receiverPort;
    doc["wifiSsid"] = g_config.wifiSsid;
    doc["wifiPass"] = g_config.wifiPass;
    doc["staConnected"] = WiFi.status() == WL_CONNECTED;
    doc["staIp"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
    doc["apSsid"] = g_apSsid;
    doc["apIp"] = WiFi.softAPIP().toString();
    doc["configVersion"] = g_configVersion;

    String payload;
    serializeJson(doc, payload);
    if (target != nullptr)
    {
        target->text(payload);
    }
    else
    {
        g_ws.textAll(payload);
    }
}

void startWebServer()
{
    g_ws.onEvent(onWsEvent);
    g_server.addHandler(&g_ws);
    g_server.serveStatic("/", LittleFS, "/");
    g_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, "/index.html", "text/html"); });
    g_server.onNotFound([](AsyncWebServerRequest *request)
                        { request->redirect("/"); });
    g_server.begin();
}

} // namespace sender
