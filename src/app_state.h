#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace receiver
{

// ============================================================
//  Hằng số chung toàn hệ thống
// ============================================================

constexpr char AP_SSID_PREFIX[] = "Receiver-";
constexpr char AP_PASS[] = "12345678";
constexpr char RECEIVER_FILE[] = "/receiver.json";
constexpr char SENDERS_FILE[] = "/senders.json";
constexpr char MQTT_GATEWAY_CONNECT_TOPIC[] = "v1/gateway/connect";
constexpr char MQTT_GATEWAY_DISCONNECT_TOPIC[] = "v1/gateway/disconnect";
constexpr char MQTT_GATEWAY_TELEMETRY_TOPIC[] = "v1/gateway/telemetry";
constexpr char MQTT_GATEWAY_ATTRIBUTES_TOPIC[] = "v1/gateway/attributes";
constexpr char MQTT_GATEWAY_RPC_TOPIC[] = "v1/gateway/rpc";
constexpr char SENDER_DEVICE_TYPE[] = "dangtai";

constexpr size_t MAX_SENDERS = 10;
constexpr size_t MAX_UNKNOWN_DEVICES = 12;
constexpr size_t MAX_TCP_CLIENTS = 14;
constexpr size_t MAX_NAME_LEN = 32;
constexpr size_t MAX_WIFI_SSID_LEN = 32;
constexpr size_t MAX_WIFI_PASS_LEN = 64;
constexpr size_t MAX_SERVER_LEN = 64;
constexpr size_t MAX_TOKEN_LEN = 128;
constexpr size_t DEVICE_ID_LEN = 18;
constexpr size_t TCP_LINE_BUFFER = 512;
constexpr size_t TCP_MAX_BYTES_PER_CYCLE = 384;
constexpr size_t TCP_MAX_LINES_PER_CYCLE = 4;
constexpr uint16_t LOCAL_TCP_PORT = 4100;
constexpr uint8_t RECEIVER_FIXED_HOST_OCTET = 100;

constexpr unsigned long SNAPSHOT_INTERVAL_MS = 1000;
constexpr unsigned long WIFI_RETRY_MS = 10000;
constexpr unsigned long MQTT_RETRY_MS = 5000;
constexpr unsigned long OFFLINE_TIMEOUT_MS = 10000;
constexpr unsigned long CONFIG_ACK_TIMEOUT_MS = 5000;
constexpr unsigned long CONFIG_RETRY_MS = 2500;

// ============================================================
//  Kiểu dữ liệu chung
// ============================================================

struct ReceiverConfig
{
    char wifiSsid[MAX_WIFI_SSID_LEN + 1] = "";
    char wifiPass[MAX_WIFI_PASS_LEN + 1] = "";
    char mqttServer[MAX_SERVER_LEN + 1] = "app.coreiot.io";
    uint16_t mqttPort = 1883;
    char gatewayToken[MAX_TOKEN_LEN + 1] = "";
};

struct SenderState
{
    bool used = false;
    bool registered = false;
    bool online = false;
    bool hasTelemetry = false;
    bool gpsValid = false;
    bool cloudConnected = false;
    bool pendingDisconnect = false;
    bool pendingConfig = false;
    bool awaitingAck = false;
    char deviceId[DEVICE_ID_LEN] = "";
    char name[MAX_NAME_LEN + 1] = "";
    float temperature = NAN;
    float humidity = NAN;
    float reportedThreshold = NAN;
    float desiredThreshold = NAN;
    float publishedThresholdAttribute = NAN;
    float aiScore = NAN;
    bool aiAnomaly = false;
    double latitude = NAN;
    double longitude = NAN;
    uint64_t senderTimestampMs = 0;
    unsigned long lastSeenMs = 0;
    unsigned long configSentAtMs = 0;
    uint32_t configVersion = 0;
    uint32_t ackedConfigVersion = 0;
    uint32_t lastRxNonce = 0;
    uint32_t lastUploadedRxNonce = 0;
    uint32_t pendingRpcId = 0;
    IPAddress publishedReceiverIp;
    IPAddress remoteIp;
    uint16_t remotePort = 0;
};

struct UnknownDevice
{
    bool used = false;
    bool online = false;
    char deviceId[DEVICE_ID_LEN] = "";
    unsigned long lastSeenMs = 0;
    uint32_t seenCount = 0;
    IPAddress remoteIp;
    uint16_t remotePort = 0;
};

struct SenderTelemetryPacket
{
    char deviceId[DEVICE_ID_LEN] = "";
    float temperature = NAN;
    float humidity = NAN;
    float threshold = NAN;
    float aiScore = NAN;
    bool aiAnomaly = false;
    double latitude = NAN;
    double longitude = NAN;
    uint64_t timestampMs = 0;
};

struct SenderConfigAckPacket
{
    char deviceId[DEVICE_ID_LEN] = "";
    float appliedThreshold = NAN;
    bool success = false;
};

struct SenderTcpClient
{
    WiFiClient socket;
    bool active = false;
    bool identified = false;
    char buffer[TCP_LINE_BUFFER] = {0};
    char deviceId[DEVICE_ID_LEN] = "";
    size_t bufferLen = 0;
    unsigned long lastSeenMs = 0;
};

// ============================================================
//  Biến trạng thái toàn firmware
// ============================================================

extern ReceiverConfig g_receiverConfig;
extern SenderState g_senders[MAX_SENDERS];
extern UnknownDevice g_unknownDevices[MAX_UNKNOWN_DEVICES];

extern DNSServer g_dnsServer;
extern AsyncWebServer g_webServer;
extern AsyncWebSocket g_ws;
extern WiFiClient g_wifiClient;
extern PubSubClient g_mqttClient;
extern WiFiServer g_senderServer;
extern SenderTcpClient g_senderClients[MAX_TCP_CLIENTS];

extern unsigned long g_lastSnapshotAt;
extern unsigned long g_lastWifiAttemptAt;
extern unsigned long g_lastMqttAttemptAt;
extern uint32_t g_rxNonce;

} // namespace receiver
