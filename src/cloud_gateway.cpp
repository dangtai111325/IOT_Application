#include "cloud_gateway.h"

#include "sender_transport.h"
#include "shared_utils.h"
#include "storage.h"

namespace receiver
{

namespace
{

String extractRpcTargetDevice(JsonDocument &doc, JsonVariant params)
{
    const String topLevel = String(doc["device"] | "");
    if (!topLevel.isEmpty())
    {
        return topLevel;
    }

    if (params.is<JsonObject>())
    {
        const String fromParams = String(params["device"] | params["deviceName"] | params["name"] | "");
        if (!fromParams.isEmpty())
        {
            return fromParams;
        }
    }

    return "";
}

JsonVariant extractRpcData(JsonDocument &doc)
{
    JsonVariant nested = doc["data"];
    if (!nested.isNull() && nested.is<JsonObject>())
    {
        return nested;
    }

    return doc.as<JsonVariant>();
}

float extractThresholdFromParams(JsonVariant params)
{
    if (params.is<JsonObject>() && !params["threshold"].isNull())
    {
        return params["threshold"].as<float>();
    }

    if (params.is<float>() || params.is<int>() || params.is<double>())
    {
        return params.as<float>();
    }

    return NAN;
}

bool mqttPublish(const char *topic, JsonDocument &doc)
{
    if (!g_mqttClient.connected())
    {
        return false;
    }

    String payload;
    serializeJson(doc, payload);
    return g_mqttClient.publish(topic, payload.c_str());
}

float resolveThresholdAttributeValue(const SenderState &sender)
{
    if (!std::isnan(sender.desiredThreshold))
    {
        return sender.desiredThreshold;
    }

    return sender.reportedThreshold;
}

bool publishGatewayConnect(const char *deviceName)
{
    JsonDocument doc;
    doc["device"] = deviceName;
    doc["type"] = SENDER_DEVICE_TYPE;
    return mqttPublish(MQTT_GATEWAY_CONNECT_TOPIC, doc);
}

bool publishSenderAttributes(int index)
{
    if (index < 0 || index >= static_cast<int>(MAX_SENDERS))
    {
        return false;
    }

    SenderState &sender = g_senders[index];
    JsonDocument doc;
    JsonObject device = doc[sender.name].to<JsonObject>();
    device["device_id"] = sender.deviceId;
    device["receiver_mac"] = WiFi.macAddress();
    device["receiver_ip"] = WiFi.localIP().toString();
    const float threshold = resolveThresholdAttributeValue(sender);
    if (!std::isnan(threshold))
    {
        device["threshold"] = threshold;
    }

    const bool published = mqttPublish(MQTT_GATEWAY_ATTRIBUTES_TOPIC, doc);
    if (published)
    {
        sender.publishedThresholdAttribute = threshold;
        sender.publishedReceiverIp = WiFi.localIP();
    }
    return published;
}

bool ensureSenderCloudSession(int index)
{
    SenderState &sender = g_senders[index];
    if (!sender.used || !sender.registered || !sender.online || sender.name[0] == '\0' || !g_mqttClient.connected())
    {
        return false;
    }

    if (!sender.cloudConnected)
    {
        if (!publishGatewayConnect(sender.name))
        {
            return false;
        }

        sender.cloudConnected = true;
        sender.pendingDisconnect = false;
        publishSenderAttributes(index);
    }

    return true;
}

bool publishSenderTelemetry(int index)
{
    SenderState &sender = g_senders[index];
    if (!ensureSenderCloudSession(index) || !sender.hasTelemetry)
    {
        return false;
    }

    JsonDocument doc;
    JsonArray rows = doc[sender.name].to<JsonArray>();
    JsonObject row = rows.add<JsonObject>();
    if (isKnownEpochMs(sender.senderTimestampMs))
    {
        row["ts"] = sender.senderTimestampMs;
    }

    JsonObject values = row["values"].to<JsonObject>();
    values["temperature"] = sender.temperature;
    values["humidity"] = sender.humidity;
    if (!std::isnan(sender.aiScore))
    {
        values["ai_score"] = sender.aiScore;
    }
    else
    {
        values["ai_score"] = nullptr;
    }
    values["ai_status"] = sender.aiAnomaly ? "ANOMALY" : "NORMAL";

    if (sender.gpsValid)
    {
        values["latitude"] = sender.latitude;
        values["longitude"] = sender.longitude;
    }

    const bool published = mqttPublish(MQTT_GATEWAY_TELEMETRY_TOPIC, doc);
    if (published)
    {
        sender.lastUploadedRxNonce = sender.lastRxNonce;
    }
    return published;
}

void handleGetThresholdRpc(const SenderState &sender, uint32_t requestId)
{
    const float threshold = !std::isnan(sender.desiredThreshold) ? sender.desiredThreshold : sender.reportedThreshold;
    publishRpcResponse(sender.name, requestId, true, "Đã đọc threshold", threshold);
}

void handleSetThresholdRpc(SenderState &sender, int index, uint32_t requestId, JsonVariant params)
{
    const float threshold = extractThresholdFromParams(params);
    if (std::isnan(threshold))
    {
        publishRpcResponse(sender.name, requestId, false, "Thiếu threshold");
        return;
    }

    if (!sender.online)
    {
        publishRpcResponse(sender.name, requestId, false, "Sender đang offline");
        return;
    }

    const float oldDesiredThreshold = sender.desiredThreshold;
    const bool oldPendingConfig = sender.pendingConfig;
    const uint32_t oldConfigVersion = sender.configVersion;
    const uint32_t oldPendingRpcId = sender.pendingRpcId;

    sender.desiredThreshold = threshold;
    sender.pendingConfig = true;
    sender.configVersion++;

    if (!sendConfigPacket(index, requestId))
    {
        sender.desiredThreshold = oldDesiredThreshold;
        sender.pendingConfig = oldPendingConfig;
        sender.configVersion = oldConfigVersion;
        sender.pendingRpcId = oldPendingRpcId;
        publishRpcResponse(sender.name, requestId, false, "Không gửi được cấu hình tới sender");
        return;
    }

    saveSendersFile();
}

void mqttCallback(char *topic, uint8_t *payload, unsigned int length)
{
    if (strcmp(topic, MQTT_GATEWAY_RPC_TOPIC) != 0)
    {
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, payload, length) != DeserializationError::Ok)
    {
        return;
    }

    JsonVariant data = extractRpcData(doc);
    const uint32_t requestId = data["id"].isNull() ? (doc["id"] | 0) : data["id"].as<uint32_t>();
    const String method = !data["method"].isNull() ? String(data["method"] | "") : String(doc["method"] | "");
    JsonVariant params;
    if (!data["params"].isNull())
    {
        params = data["params"];
    }
    else
    {
        params = doc["params"];
    }
    const String deviceName = extractRpcTargetDevice(doc, params);

    if (deviceName.isEmpty())
    {
        publishRpcResponse("", requestId, false, "Thiếu tên sender trong payload RPC");
        return;
    }

    const int index = findSenderByName(deviceName);
    if (index < 0)
    {
        publishRpcResponse(deviceName.c_str(), requestId, false, "Không tìm thấy sender");
        return;
    }

    SenderState &sender = g_senders[index];

    if (method.equalsIgnoreCase("getThreshold"))
    {
        handleGetThresholdRpc(sender, requestId);
        return;
    }

    if (method.equalsIgnoreCase("setThreshold"))
    {
        handleSetThresholdRpc(sender, index, requestId, params);
        return;
    }

    publishRpcResponse(deviceName.c_str(), requestId, false, "RPC không được hỗ trợ");
}

} // namespace

String cloudStatusText()
{
    if (strlen(g_receiverConfig.gatewayToken) == 0 || strlen(g_receiverConfig.mqttServer) == 0)
    {
        return "Thiếu cấu hình";
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        return "Đang chờ Wi-Fi";
    }

    return g_mqttClient.connected() ? "Đã kết nối" : "Đang kết nối";
}

bool publishGatewayDisconnect(const char *deviceName)
{
    JsonDocument doc;
    doc["device"] = deviceName;
    return mqttPublish(MQTT_GATEWAY_DISCONNECT_TOPIC, doc);
}

void publishRpcResponse(const char *deviceName, uint32_t requestId, bool success, const char *message, float threshold)
{
    if (!g_mqttClient.connected())
    {
        return;
    }

    JsonDocument doc;
    doc["device"] = deviceName;
    doc["id"] = requestId;
    JsonObject data = doc["data"].to<JsonObject>();
    data["success"] = success;

    if (message != nullptr && message[0] != '\0')
    {
        data["message"] = message;
    }

    if (!std::isnan(threshold))
    {
        data["threshold"] = threshold;
    }

    mqttPublish(MQTT_GATEWAY_RPC_TOPIC, doc);
}

void maintainMqtt()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    if (strlen(g_receiverConfig.gatewayToken) == 0 || strlen(g_receiverConfig.mqttServer) == 0)
    {
        return;
    }

    g_mqttClient.setServer(g_receiverConfig.mqttServer, g_receiverConfig.mqttPort);
    g_mqttClient.setCallback(mqttCallback);
    g_mqttClient.setBufferSize(1024);

    if (!g_mqttClient.connected())
    {
        if (millis() - g_lastMqttAttemptAt < MQTT_RETRY_MS)
        {
            return;
        }

        g_lastMqttAttemptAt = millis();
        const String clientId = "receiver-" + WiFi.macAddress();
        if (g_mqttClient.connect(clientId.c_str(), g_receiverConfig.gatewayToken, nullptr))
        {
            g_mqttClient.subscribe(MQTT_GATEWAY_RPC_TOPIC);
            resetCloudSessionFlags();
        }
        return;
    }

    g_mqttClient.loop();

    for (int i = 0; i < static_cast<int>(MAX_SENDERS); ++i)
    {
        SenderState &sender = g_senders[i];
        if (sender.pendingDisconnect && sender.name[0] != '\0')
        {
            if (publishGatewayDisconnect(sender.name))
            {
                sender.pendingDisconnect = false;
            }
        }

        if (!sender.used || !sender.registered || !sender.online)
        {
            continue;
        }

        ensureSenderCloudSession(i);
        const float threshold = resolveThresholdAttributeValue(sender);
        const bool receiverIpChanged = sender.publishedReceiverIp != WiFi.localIP();
        const bool shouldPublishThresholdAttribute =
            !std::isnan(threshold) &&
            (std::isnan(sender.publishedThresholdAttribute) ||
             fabsf(sender.publishedThresholdAttribute - threshold) > 0.0001f);

        if (shouldPublishThresholdAttribute || receiverIpChanged)
        {
            publishSenderAttributes(i);
        }

        if (sender.lastUploadedRxNonce != sender.lastRxNonce)
        {
            publishSenderTelemetry(i);
        }
    }
}

} // namespace receiver
