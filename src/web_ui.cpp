#include "web_ui.h"

#include "cloud_gateway.h"
#include "receiver_runtime.h"
#include "shared_utils.h"
#include "storage.h"

namespace receiver
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

void handleSaveReceiverConfig(AsyncWebSocketClient *client, JsonDocument &doc)
{
    copyStringToBuffer(String(doc["ssid"] | ""), g_receiverConfig.wifiSsid, sizeof(g_receiverConfig.wifiSsid));
    copyStringToBuffer(String(doc["pass"] | ""), g_receiverConfig.wifiPass, sizeof(g_receiverConfig.wifiPass));
    copyStringToBuffer(String(doc["server"] | "app.coreiot.io"), g_receiverConfig.mqttServer, sizeof(g_receiverConfig.mqttServer));
    g_receiverConfig.mqttPort = doc["port"] | 1883;
    copyStringToBuffer(String(doc["token"] | ""), g_receiverConfig.gatewayToken, sizeof(g_receiverConfig.gatewayToken));

    sendActionResult(client, true, "Đã lưu cấu hình receiver. Thiết bị sẽ khởi động lại.");
    saveReceiverConfig(true);
}

void handleAddOrUpdateDevice(AsyncWebSocketClient *client, JsonDocument &doc)
{
    String error;
    const String deviceId = String(doc["deviceId"] | doc["mac"] | "");
    const float threshold = doc["threshold"].isNull() ? NAN : doc["threshold"].as<float>();
    if (!saveSenderConfigFromUi(deviceId, String(doc["name"] | ""), threshold, error))
    {
        sendActionResult(client, false, error);
        return;
    }

    sendActionResult(client, true, "Đã lưu cấu hình sender.");
    pushSnapshot();
}

void handleDeleteDevice(AsyncWebSocketClient *client, JsonDocument &doc)
{
    String error;
    const String deviceId = String(doc["deviceId"] | doc["mac"] | "");
    if (!deleteSenderFromUi(deviceId, error))
    {
        sendActionResult(client, false, error);
        return;
    }

    sendActionResult(client, true, "Đã xóa sender khỏi danh sách.");
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
    if (action == "saveReceiverConfig")
    {
        handleSaveReceiverConfig(client, doc);
    }
    else if (action == "addDevice" || action == "saveSenderConfig")
    {
        handleAddOrUpdateDevice(client, doc);
    }
    else if (action == "deleteSender")
    {
        handleDeleteDevice(client, doc);
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
        pushSnapshot(client);
        return;
    }

    if (type == WS_EVT_DATA)
    {
        handleWsMessage(client, data, len);
    }
}

void appendSenderJson(JsonArray &senders, const SenderState &sender)
{
    JsonObject item = senders.add<JsonObject>();
    item["deviceId"] = sender.deviceId;
    item["name"] = senderDisplayName(sender);
    if (static_cast<uint32_t>(sender.remoteIp) == 0)
    {
        item["ip"] = nullptr;
    }
    else
    {
        item["ip"] = sender.remoteIp.toString();
    }
    item["registered"] = sender.registered;
    item["online"] = sender.online;
    item["syncPending"] = sender.pendingConfig || sender.awaitingAck;
    if (sender.hasTelemetry)
    {
        item["temperature"] = sender.temperature;
        item["humidity"] = sender.humidity;
    }
    else
    {
        item["temperature"] = nullptr;
        item["humidity"] = nullptr;
    }
    if (sender.hasTelemetry && !std::isnan(sender.aiScore))
    {
        item["aiScore"] = sender.aiScore;
    }
    else
    {
        item["aiScore"] = nullptr;
    }
    if (!std::isnan(sender.reportedThreshold))
    {
        item["reportedThreshold"] = sender.reportedThreshold;
    }
    else
    {
        item["reportedThreshold"] = nullptr;
    }
    if (!std::isnan(sender.desiredThreshold))
    {
        item["desiredThreshold"] = sender.desiredThreshold;
    }
    else
    {
        item["desiredThreshold"] = nullptr;
    }
    item["aiStatus"] = sender.hasTelemetry ? (sender.aiAnomaly ? "ANOMALY" : "NORMAL") : "NO DATA";
    if (sender.gpsValid)
    {
        item["latitude"] = sender.latitude;
        item["longitude"] = sender.longitude;
    }
    else
    {
        item["latitude"] = nullptr;
        item["longitude"] = nullptr;
    }
    item["hasTelemetry"] = sender.hasTelemetry;
    item["lastSeenAgeMs"] = sender.lastSeenMs == 0 ? 0 : millis() - sender.lastSeenMs;
    item["senderTimestampMs"] = sender.senderTimestampMs;
    item["configVersion"] = sender.configVersion;
}

void appendUnknownJson(JsonArray &unknownArray, const UnknownDevice &device)
{
    JsonObject item = unknownArray.add<JsonObject>();
    item["deviceId"] = device.deviceId;
    item["online"] = device.online;
    item["lastSeenAgeMs"] = device.lastSeenMs == 0 ? 0 : millis() - device.lastSeenMs;
    item["seenCount"] = device.seenCount;
    item["ip"] = device.remoteIp.toString();
}

} // namespace

void pushSnapshot(AsyncWebSocketClient *target)
{
    JsonDocument doc;
    doc["type"] = "snapshot";

    JsonObject receiver = doc["receiver"].to<JsonObject>();
    receiver["wifiSsid"] = g_receiverConfig.wifiSsid;
    receiver["wifiPass"] = g_receiverConfig.wifiPass;
    receiver["server"] = g_receiverConfig.mqttServer;
    receiver["port"] = g_receiverConfig.mqttPort;
    receiver["token"] = g_receiverConfig.gatewayToken;    receiver["cloudStatus"] = cloudStatusText();
    receiver["wifiConnected"] = WiFi.status() == WL_CONNECTED;
    receiver["wifiIp"] = WiFi.localIP().toString();
    receiver["apIp"] = WiFi.softAPIP().toString();
    receiver["receiverMac"] = WiFi.macAddress();
    receiver["channel"] = currentWifiChannel();
    receiver["localTcpPort"] = LOCAL_TCP_PORT;

    JsonArray senders = doc["senders"].to<JsonArray>();
    for (const SenderState &sender : g_senders)
    {
        if (!sender.used || !sender.registered)
        {
            continue;
        }

        appendSenderJson(senders, sender);
    }

    JsonArray unknownDevices = doc["unknownDevices"].to<JsonArray>();
    for (const UnknownDevice &device : g_unknownDevices)
    {
        if (!device.used)
        {
            continue;
        }

        appendUnknownJson(unknownDevices, device);
    }

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
    g_webServer.addHandler(&g_ws);
    g_webServer.serveStatic("/", LittleFS, "/");
    g_webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                   { request->send(LittleFS, "/index.html", "text/html"); });
    g_webServer.onNotFound([](AsyncWebServerRequest *request)
                           { request->redirect("/"); });
    g_webServer.begin();
}

} // namespace receiver

