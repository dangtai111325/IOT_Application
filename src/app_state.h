#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

namespace sender
{

constexpr char CONFIG_FILE[] = "/sender.json";
constexpr uint16_t DEFAULT_RECEIVER_PORT = 4100;
constexpr uint32_t SNAPSHOT_INTERVAL_MS = 1000;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 1000;
constexpr uint32_t SENSOR_INTERVAL_MS = 1000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 8000;
constexpr uint32_t TCP_RETRY_INTERVAL_MS = 3000;
constexpr size_t MAX_TEXT_LEN = 64;
constexpr size_t TCP_RX_BUFFER_LEN = 384;
constexpr int AI_TENSOR_ARENA_SIZE = 8 * 1024;

extern const IPAddress AP_IP;
extern const IPAddress AP_GATEWAY;
extern const IPAddress AP_SUBNET;

struct SenderConfig
{
    char wifiSsid[MAX_TEXT_LEN] = "";
    char wifiPass[MAX_TEXT_LEN] = "";
    char receiverIp[32] = "";
    uint16_t receiverPort = DEFAULT_RECEIVER_PORT;
    float threshold = 0.5f;
    char deviceName[MAX_TEXT_LEN] = "Undefined";
};

extern SenderConfig g_config;

extern AsyncWebServer g_server;
extern AsyncWebSocket g_ws;
extern WiFiClient g_tcpClient;

extern String g_deviceId;
extern String g_apSsid;
extern String g_receiverStatus;
extern String g_aiStatus;
extern String g_tcpRxBuffer;

extern float g_temperature;
extern float g_humidity;
extern float g_aiScore;
extern double g_latitude;
extern double g_longitude;

extern bool g_wsConnected;
extern bool g_wifiConnecting;

extern uint32_t g_configVersion;
extern uint32_t g_lastSensorAt;
extern uint32_t g_lastTelemetryAt;
extern uint32_t g_lastSnapshotAt;
extern uint32_t g_lastWifiAttemptAt;
extern uint32_t g_lastTcpAttemptAt;

template <size_t N>
inline void copyToBuffer(const String &value, char (&buffer)[N])
{
    value.substring(0, N - 1).toCharArray(buffer, N);
}

String normalizeMac(const String &mac);
bool parseIpAddress(const char *text, IPAddress &ip);
String inferReceiverIp();
String resolvedReceiverIp();
bool hasReceiverConfig();
uint64_t senderTimestampMs();
void markReceiverStatus();

} // namespace sender
