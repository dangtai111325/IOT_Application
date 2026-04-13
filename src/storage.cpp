#include "storage.h"

#include "shared_utils.h"

namespace receiver
{

void loadReceiverConfig()
{
    if (!LittleFS.exists(RECEIVER_FILE))
    {
        return;
    }

    File file = LittleFS.open(RECEIVER_FILE, "r");
    if (!file)
    {
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, file) == DeserializationError::Ok)
    {
        copyStringToBuffer(String(doc["wifi_ssid"] | ""), g_receiverConfig.wifiSsid, sizeof(g_receiverConfig.wifiSsid));
        copyStringToBuffer(String(doc["wifi_pass"] | ""), g_receiverConfig.wifiPass, sizeof(g_receiverConfig.wifiPass));
        copyStringToBuffer(String(doc["mqtt_server"] | "app.coreiot.io"), g_receiverConfig.mqttServer, sizeof(g_receiverConfig.mqttServer));
        g_receiverConfig.mqttPort = doc["mqtt_port"] | 1883;
        copyStringToBuffer(String(doc["gateway_token"] | ""), g_receiverConfig.gatewayToken, sizeof(g_receiverConfig.gatewayToken));
    }

    file.close();
}

void saveReceiverConfig(bool rebootAfterSave)
{
    JsonDocument doc;
    doc["wifi_ssid"] = g_receiverConfig.wifiSsid;
    doc["wifi_pass"] = g_receiverConfig.wifiPass;
    doc["mqtt_server"] = g_receiverConfig.mqttServer;
    doc["mqtt_port"] = g_receiverConfig.mqttPort;
    doc["gateway_token"] = g_receiverConfig.gatewayToken;

    File file = LittleFS.open(RECEIVER_FILE, "w");
    if (file)
    {
        serializeJson(doc, file);
        file.close();
    }

    if (rebootAfterSave)
    {
        delay(300);
        ESP.restart();
    }
}

void saveSendersFile()
{
    JsonDocument doc;
    JsonArray senders = doc.to<JsonArray>();

    for (const SenderState &sender : g_senders)
    {
        if (!sender.used || !sender.registered)
        {
            continue;
        }

        JsonObject item = senders.add<JsonObject>();
        item["device_id"] = sender.deviceId;
        item["name"] = sender.name;
        if (!std::isnan(sender.desiredThreshold))
        {
            item["desired_threshold"] = sender.desiredThreshold;
        }
        else
        {
            item["desired_threshold"] = nullptr;
        }
        item["pending_config"] = sender.pendingConfig;
        item["config_version"] = sender.configVersion;
        item["acked_config_version"] = sender.ackedConfigVersion;
    }

    File file = LittleFS.open(SENDERS_FILE, "w");
    if (file)
    {
        serializeJson(doc, file);
        file.close();
    }
}

void loadSendersFile()
{
    if (!LittleFS.exists(SENDERS_FILE))
    {
        return;
    }

    File file = LittleFS.open(SENDERS_FILE, "r");
    if (!file)
    {
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, file) != DeserializationError::Ok || !doc.is<JsonArray>())
    {
        file.close();
        return;
    }
    file.close();

    for (JsonObject item : doc.as<JsonArray>())
    {
        const String deviceId = normalizeDeviceId(String(item["device_id"] | ""));
        if (!isValidDeviceId(deviceId))
        {
            continue;
        }

        const int slot = findFreeSenderSlot();
        if (slot < 0)
        {
            break;
        }

        SenderState &sender = g_senders[slot];
        sender = SenderState();
        sender.used = true;
        sender.registered = true;
        copyStringToBuffer(deviceId, sender.deviceId, sizeof(sender.deviceId));
        copyStringToBuffer(String(item["name"] | ""), sender.name, sizeof(sender.name));
        sender.pendingConfig = item["pending_config"] | false;
        sender.awaitingAck = false;
        sender.configVersion = item["config_version"] | 0;
        sender.ackedConfigVersion = item["acked_config_version"] | 0;
        sender.desiredThreshold = item["desired_threshold"].isNull() ? NAN : item["desired_threshold"].as<float>();
    }
}

} // namespace receiver
