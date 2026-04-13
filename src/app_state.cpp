#include "app_state.h"

namespace receiver
{

ReceiverConfig g_receiverConfig;
SenderState g_senders[MAX_SENDERS];
UnknownDevice g_unknownDevices[MAX_UNKNOWN_DEVICES];

DNSServer g_dnsServer;
AsyncWebServer g_webServer(80);
AsyncWebSocket g_ws("/ws");
WiFiClient g_wifiClient;
PubSubClient g_mqttClient(g_wifiClient);
WiFiServer g_senderServer(LOCAL_TCP_PORT);
SenderTcpClient g_senderClients[MAX_TCP_CLIENTS];

bool g_cloudEnabled = true;
unsigned long g_lastSnapshotAt = 0;
unsigned long g_lastWifiAttemptAt = 0;
unsigned long g_lastMqttAttemptAt = 0;
uint32_t g_rxNonce = 0;

} // namespace receiver

