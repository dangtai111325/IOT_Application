#include "receiver_runtime.h"

#include "cloud_gateway.h"
#include "sender_transport.h"
#include "shared_utils.h"
#include "storage.h"

namespace receiver
{

namespace
{

IPAddress s_apIp(192, 168, 7, 1);
IPAddress s_apGateway(192, 168, 7, 1);
IPAddress s_apSubnet(255, 255, 255, 0);

bool s_staStaticLocked = false;
bool s_staStaticConfigured = false;
IPAddress s_staStaticIp;
IPAddress s_staGateway;
IPAddress s_staSubnet;
IPAddress s_staDns1;
IPAddress s_staDns2;

float resolveThresholdForSender(const SenderState &sender, float thresholdFromUi)
{
    if (!std::isnan(thresholdFromUi))
    {
        return thresholdFromUi;
    }

    if (!std::isnan(sender.reportedThreshold))
    {
        return sender.reportedThreshold;
    }

    if (!std::isnan(sender.desiredThreshold))
    {
        return sender.desiredThreshold;
    }

    return 0.5f;
}

void markUnknownDevicesOffline()
{
    for (UnknownDevice &device : g_unknownDevices)
    {
        if (!device.used || !device.online)
        {
            continue;
        }

        if (millis() - device.lastSeenMs > OFFLINE_TIMEOUT_MS)
        {
            device.online = false;
        }
    }
}

bool isZeroIp(const IPAddress &ip)
{
    return static_cast<uint32_t>(ip) == 0;
}

bool isCommon24BitSubnet(const IPAddress &subnet)
{
    return subnet[0] == 255 && subnet[1] == 255 && subnet[2] == 255 && subnet[3] == 0;
}

void beginWifiStation()
{
    if (strlen(g_receiverConfig.wifiSsid) == 0)
    {
        return;
    }

    if (s_staStaticConfigured)
    {
        WiFi.config(s_staStaticIp, s_staGateway, s_staSubnet, s_staDns1, s_staDns2);
    }

    WiFi.begin(g_receiverConfig.wifiSsid, g_receiverConfig.wifiPass);
}

void tryLockStaIpToDot100()
{
    if (s_staStaticLocked || WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    const IPAddress currentIp = WiFi.localIP();
    const IPAddress gateway = WiFi.gatewayIP();
    const IPAddress subnet = WiFi.subnetMask();
    const IPAddress dns1 = WiFi.dnsIP(0);
    const IPAddress dns2 = WiFi.dnsIP(1);

    if (isZeroIp(currentIp) || isZeroIp(gateway) || !isCommon24BitSubnet(subnet))
    {
        s_staStaticLocked = true;
        return;
    }

    IPAddress desiredIp(currentIp[0], currentIp[1], currentIp[2], RECEIVER_FIXED_HOST_OCTET);
    if (desiredIp == currentIp)
    {
        s_staStaticLocked = true;
        s_staStaticConfigured = true;
        s_staStaticIp = desiredIp;
        s_staGateway = gateway;
        s_staSubnet = subnet;
        s_staDns1 = dns1;
        s_staDns2 = dns2;
        return;
    }

    if (desiredIp == gateway || desiredIp[3] == 0 || desiredIp[3] == 255)
    {
        s_staStaticLocked = true;
        return;
    }

    s_staStaticIp = desiredIp;
    s_staGateway = gateway;
    s_staSubnet = subnet;
    s_staDns1 = dns1;
    s_staDns2 = dns2;
    s_staStaticConfigured = true;
    s_staStaticLocked = true;

    WiFi.disconnect();
    WiFi.config(s_staStaticIp, s_staGateway, s_staSubnet, s_staDns1, s_staDns2);
    WiFi.begin(g_receiverConfig.wifiSsid, g_receiverConfig.wifiPass);
    g_lastWifiAttemptAt = millis();
}

} // namespace

void setupNetworking()
{
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(s_apIp, s_apGateway, s_apSubnet);
    String apSsid = AP_SSID_PREFIX + normalizeDeviceId(WiFi.macAddress()).substring(9);
    apSsid.replace(":", "");
    WiFi.softAP(apSsid.c_str(), AP_PASS);
    g_dnsServer.start(53, "*", WiFi.softAPIP());

    beginWifiStation();
}

void maintainWifi()
{
    if (strlen(g_receiverConfig.wifiSsid) == 0)
    {
        return;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        tryLockStaIpToDot100();
        return;
    }

    if (millis() - g_lastWifiAttemptAt < WIFI_RETRY_MS)
    {
        return;
    }

    g_lastWifiAttemptAt = millis();
    WiFi.disconnect();
    beginWifiStation();
}

void handleSenderOfflineTransitions()
{
    for (SenderState &sender : g_senders)
    {
        if (!sender.used || !sender.online)
        {
            continue;
        }

        if (millis() - sender.lastSeenMs > OFFLINE_TIMEOUT_MS)
        {
            sender.online = false;
            sender.pendingDisconnect = sender.cloudConnected && sender.registered && sender.name[0] != '\0';
            if (sender.pendingDisconnect && g_mqttClient.connected())
            {
                sender.pendingDisconnect = !publishGatewayDisconnect(sender.name);
            }
            sender.cloudConnected = false;
        }
    }

    markUnknownDevicesOffline();
}

void processConfigRetries()
{
    for (int i = 0; i < static_cast<int>(MAX_SENDERS); ++i)
    {
        SenderState &sender = g_senders[i];
        if (!sender.used || !sender.registered || !sender.pendingConfig || !sender.online)
        {
            continue;
        }

        if (sender.awaitingAck)
        {
            if (millis() - sender.configSentAtMs > CONFIG_ACK_TIMEOUT_MS)
            {
                sender.awaitingAck = false;
                if (sender.pendingRpcId != 0)
                {
                    publishRpcResponse(sender.name, sender.pendingRpcId, false, "Sender không ACK kịp thời gian");
                    sender.pendingRpcId = 0;
                }
            }
            continue;
        }

        if (millis() - sender.configSentAtMs < CONFIG_RETRY_MS)
        {
            continue;
        }

        sendConfigPacket(i, 0);
    }
}

bool saveSenderConfigFromUi(const String &rawDeviceId, const String &name, float threshold, String &error)
{
    const String deviceId = normalizeDeviceId(rawDeviceId);
    if (!isValidDeviceId(deviceId))
    {
        error = "Device ID phải là địa chỉ MAC hợp lệ.";
        return false;
    }

    String trimmedName = trimName(name);
    if (trimmedName.isEmpty())
    {
        error = "Tên sender không được để trống.";
        return false;
    }

    int index = findSenderByDeviceId(deviceId);
    if (index < 0)
    {
        index = findFreeSenderSlot();
        if (index < 0)
        {
            error = "Danh sách sender đã đầy.";
            return false;
        }
    }

    if (!isNameUnique(trimmedName, index))
    {
        error = "Tên sender đã tồn tại.";
        return false;
    }

    const int unknownIndex = findUnknownByDeviceId(deviceId);
    SenderState previous = g_senders[index];
    SenderState &sender = g_senders[index];
    if (!sender.used)
    {
        sender = SenderState();
        sender.used = true;
    }

    copyStringToBuffer(deviceId, sender.deviceId, sizeof(sender.deviceId));
    copyStringToBuffer(trimmedName, sender.name, sizeof(sender.name));
    sender.registered = true;
    sender.desiredThreshold = resolveThresholdForSender(previous, threshold);
    sender.pendingConfig = true;
    sender.configVersion = previous.configVersion + 1;
    sender.ackedConfigVersion = previous.ackedConfigVersion;
    sender.hasTelemetry = previous.hasTelemetry;
    sender.online = previous.online;
    sender.temperature = previous.temperature;
    sender.humidity = previous.humidity;
    sender.reportedThreshold = previous.reportedThreshold;
    sender.aiScore = previous.aiScore;
    sender.aiAnomaly = previous.aiAnomaly;
    sender.latitude = previous.latitude;
    sender.longitude = previous.longitude;
    sender.gpsValid = previous.gpsValid;
    sender.lastSeenMs = previous.lastSeenMs;
    sender.senderTimestampMs = previous.senderTimestampMs;
    sender.remoteIp = previous.remoteIp;
    sender.remotePort = previous.remotePort;
    sender.lastRxNonce = previous.lastRxNonce;
    sender.lastUploadedRxNonce = previous.lastUploadedRxNonce;

    if (unknownIndex >= 0)
    {
        const UnknownDevice &unknown = g_unknownDevices[unknownIndex];
        sender.online = sender.online || unknown.online;
        if (static_cast<uint32_t>(unknown.remoteIp) != 0)
        {
            sender.remoteIp = unknown.remoteIp;
            sender.remotePort = unknown.remotePort;
        }
        if (sender.lastSeenMs == 0)
        {
            sender.lastSeenMs = unknown.lastSeenMs;
        }
    }

    if (previous.registered && previous.name[0] != '\0' && !String(previous.name).equals(trimmedName) &&
        previous.cloudConnected && g_mqttClient.connected())
    {
        publishGatewayDisconnect(previous.name);
        sender.cloudConnected = false;
        sender.pendingDisconnect = false;
    }

    removeUnknownDevice(deviceId);
    saveSendersFile();

    if (sender.online)
    {
        sendConfigPacket(index, 0);
    }

    return true;
}

bool deleteSenderFromUi(const String &rawDeviceId, String &error)
{
    const String deviceId = normalizeDeviceId(rawDeviceId);
    const int index = findSenderByDeviceId(deviceId);
    if (index < 0)
    {
        error = "Không tìm thấy sender cần xóa.";
        return false;
    }

    SenderState previous = g_senders[index];
    if (previous.cloudConnected && g_mqttClient.connected() && previous.name[0] != '\0')
    {
        publishGatewayDisconnect(previous.name);
    }

    g_senders[index] = SenderState();
    saveSendersFile();
    return true;
}

} // namespace receiver
