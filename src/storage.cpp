#include "storage.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

#include "app_state.h"

namespace sender
{

void saveConfig(bool restartAfterSave)
{
    JsonDocument doc;
    doc["wifiSsid"] = g_config.wifiSsid;
    doc["wifiPass"] = g_config.wifiPass;
    doc["receiverIp"] = g_config.receiverIp;
    doc["receiverPort"] = g_config.receiverPort;
    doc["threshold"] = g_config.threshold;
    doc["deviceName"] = g_config.deviceName;

    File file = LittleFS.open(CONFIG_FILE, "w");
    if (file)
    {
        serializeJson(doc, file);
        file.close();
    }

    if (restartAfterSave)
    {
        delay(300);
        ESP.restart();
    }
}

void loadConfig()
{
    if (!LittleFS.exists(CONFIG_FILE))
    {
        return;
    }

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file)
    {
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, file) != DeserializationError::Ok)
    {
        file.close();
        return;
    }

    copyToBuffer(String(doc["wifiSsid"] | ""), g_config.wifiSsid);
    copyToBuffer(String(doc["wifiPass"] | ""), g_config.wifiPass);
    copyToBuffer(String(doc["receiverIp"] | ""), g_config.receiverIp);
    g_config.receiverPort = doc["receiverPort"] | DEFAULT_RECEIVER_PORT;
    g_config.threshold = doc["threshold"] | 0.5f;
    copyToBuffer(String(doc["deviceName"] | "Undefined"), g_config.deviceName);
    file.close();
}

} // namespace sender
