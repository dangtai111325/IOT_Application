#include "app_state.h"

namespace sender
{

const IPAddress AP_IP(192, 168, 7, 1);
const IPAddress AP_GATEWAY(192, 168, 7, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

SenderConfig g_config;

AsyncWebServer g_server(80);
AsyncWebSocket g_ws("/ws");
WiFiClient g_tcpClient;

String g_deviceId;
String g_apSsid;
String g_receiverStatus = "Chưa cấu hình";
String g_aiStatus = "NORMAL";
String g_tcpRxBuffer;

float g_temperature = 29.8f;
float g_humidity = 67.5f;
float g_aiScore = 0.0f;
double g_latitude = 10.762622;
double g_longitude = 106.660172;

bool g_wsConnected = false;
bool g_wifiConnecting = false;

uint32_t g_configVersion = 0;
uint32_t g_lastSensorAt = 0;
uint32_t g_lastTelemetryAt = 0;
uint32_t g_lastSnapshotAt = 0;
uint32_t g_lastWifiAttemptAt = 0;
uint32_t g_lastTcpAttemptAt = 0;

String normalizeMac(const String &mac)
{
    String normalized = mac;
    normalized.trim();
    normalized.toUpperCase();
    return normalized;
}

bool parseIpAddress(const char *text, IPAddress &ip)
{
    return ip.fromString(text);
}

String inferReceiverIp()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return "";
    }

    const IPAddress currentIp = WiFi.localIP();
    const IPAddress subnet = WiFi.subnetMask();
    if (static_cast<uint32_t>(currentIp) == 0)
    {
        return "";
    }

    const bool is24BitSubnet = subnet[0] == 255 && subnet[1] == 255 && subnet[2] == 255 && subnet[3] == 0;
    if (!is24BitSubnet)
    {
        return "";
    }

    IPAddress inferred(currentIp[0], currentIp[1], currentIp[2], 100);
    return inferred.toString();
}

String resolvedReceiverIp()
{
    if (strlen(g_config.receiverIp) > 0)
    {
        return String(g_config.receiverIp);
    }

    return inferReceiverIp();
}

bool hasReceiverConfig()
{
    IPAddress ip;
    return parseIpAddress(resolvedReceiverIp().c_str(), ip);
}

uint64_t senderTimestampMs()
{
    return 1700000000000ULL + millis();
}

void markReceiverStatus()
{
    if (strlen(g_config.wifiSsid) == 0)
    {
        g_receiverStatus = "Thiếu Wi-Fi";
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        g_receiverStatus = "Đang chờ Wi-Fi";
        return;
    }

    if (!hasReceiverConfig())
    {
        g_receiverStatus = "Chưa suy ra được IP receiver";
        return;
    }

    g_receiverStatus = g_tcpClient.connected() ? "Đã kết nối receiver" : "Đang kết nối receiver";
}

} // namespace sender
