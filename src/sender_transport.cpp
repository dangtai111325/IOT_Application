#include "sender_transport.h"

#include <ArduinoJson.h>

#include "app_state.h"
#include "storage.h"

namespace sender
{

namespace
{

void sendConfigAck(bool success, float appliedThreshold, const char *message = nullptr)
{
    if (!g_tcpClient.connected())
    {
        return;
    }

    JsonDocument doc;
    doc["type"] = "config_ack";
    doc["device_id"] = g_deviceId;
    doc["success"] = success;
    doc["applied_threshold"] = appliedThreshold;
    doc["config_version"] = g_configVersion;

    if (message != nullptr && message[0] != '\0')
    {
        doc["message"] = message;
    }

    String payload;
    serializeJson(doc, payload);
    payload += '\n';
    g_tcpClient.print(payload);
}

void applyConfigFromReceiver(JsonDocument &doc)
{
    const String deviceId = normalizeMac(String(doc["device_id"] | ""));
    if (deviceId != g_deviceId)
    {
        return;
    }

    bool changed = false;

    if (!doc["name"].isNull())
    {
        const String name = String(doc["name"].as<const char *>());
        if (name.length() > 0 && String(g_config.deviceName) != name)
        {
            copyToBuffer(name, g_config.deviceName);
            changed = true;
        }
    }

    if (!doc["threshold"].isNull())
    {
        const float threshold = doc["threshold"].as<float>();
        if (std::isnan(threshold) || threshold < 0.0f || threshold > 1.0f)
        {
            sendConfigAck(false, g_config.threshold, "Threshold không hợp lệ");
            return;
        }

        if (fabsf(g_config.threshold - threshold) > 0.0001f)
        {
            g_config.threshold = threshold;
            changed = true;
        }
    }

    if (!doc["receiver_ip"].isNull())
    {
        const String ip = String(doc["receiver_ip"].as<const char *>());
        if (ip.length() > 0 && String(g_config.receiverIp) != ip)
        {
            copyToBuffer(ip, g_config.receiverIp);
            changed = true;
        }
    }

    if (!doc["receiver_port"].isNull())
    {
        const uint16_t port = doc["receiver_port"].as<uint16_t>();
        if (port > 0 && g_config.receiverPort != port)
        {
            g_config.receiverPort = port;
            changed = true;
        }
    }

    if (!doc["config_version"].isNull())
    {
        g_configVersion = doc["config_version"].as<uint32_t>();
    }

    if (changed)
    {
        saveConfig(false);
    }

    sendConfigAck(true, g_config.threshold, "Đã áp dụng cấu hình");
}

void processIncomingLine(const String &line)
{
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok)
    {
        return;
    }

    const String type = String(doc["type"] | "");
    if (type.equalsIgnoreCase("config"))
    {
        applyConfigFromReceiver(doc);
    }
}

void pumpTcpInput()
{
    while (g_tcpClient.connected() && g_tcpClient.available() > 0)
    {
        const char ch = static_cast<char>(g_tcpClient.read());
        if (ch == '\r')
        {
            continue;
        }

        if (ch == '\n')
        {
            if (g_tcpRxBuffer.length() > 0)
            {
                processIncomingLine(g_tcpRxBuffer);
                g_tcpRxBuffer = "";
            }
            continue;
        }

        if (g_tcpRxBuffer.length() < TCP_RX_BUFFER_LEN)
        {
            g_tcpRxBuffer += ch;
        }
    }
}

void sendTelemetry()
{
    if (!g_tcpClient.connected() || millis() - g_lastTelemetryAt < TELEMETRY_INTERVAL_MS)
    {
        return;
    }

    g_lastTelemetryAt = millis();

    JsonDocument doc;
    doc["type"] = "telemetry";
    doc["device_id"] = g_deviceId;
    doc["temperature"] = g_temperature;
    doc["humidity"] = g_humidity;
    doc["threshold"] = g_config.threshold;
    doc["ai_score"] = g_aiScore;
    doc["ai_status"] = g_aiStatus;
    doc["latitude"] = g_latitude;
    doc["longitude"] = g_longitude;
    doc["timestamp"] = senderTimestampMs();

    String payload;
    serializeJson(doc, payload);
    payload += '\n';
    g_tcpClient.print(payload);
}

} // namespace

void setupSoftAp()
{
    WiFi.softAPdisconnect(true);
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
    WiFi.softAP(g_apSsid.c_str());
}

void maintainWifiSta()
{
    if (strlen(g_config.wifiSsid) == 0)
    {
        g_wifiConnecting = false;
        return;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        g_wifiConnecting = false;
        return;
    }

    if (millis() - g_lastWifiAttemptAt < WIFI_RETRY_INTERVAL_MS && g_wifiConnecting)
    {
        return;
    }

    g_lastWifiAttemptAt = millis();
    g_wifiConnecting = true;
    WiFi.begin(g_config.wifiSsid, g_config.wifiPass);
}

void maintainTcp()
{
    if (WiFi.status() != WL_CONNECTED || !hasReceiverConfig())
    {
        if (g_tcpClient.connected())
        {
            g_tcpClient.stop();
        }
        markReceiverStatus();
        return;
    }

    if (!g_tcpClient.connected())
    {
        if (millis() - g_lastTcpAttemptAt < TCP_RETRY_INTERVAL_MS)
        {
            markReceiverStatus();
            return;
        }

        g_lastTcpAttemptAt = millis();
        IPAddress receiverIp;
        const String targetIp = resolvedReceiverIp();
        if (parseIpAddress(targetIp.c_str(), receiverIp))
        {
            g_tcpClient.setNoDelay(true);
            g_tcpClient.connect(receiverIp, g_config.receiverPort);
        }
    }

    pumpTcpInput();
    sendTelemetry();
    markReceiverStatus();
}

} // namespace sender
