#include "sender_transport.h"

#include "cloud_gateway.h"
#include "shared_utils.h"
#include "storage.h"

namespace receiver
{

namespace
{

void releaseClientSlot(int index);

int allocateClientSlot()
{
    for (size_t i = 0; i < MAX_TCP_CLIENTS; ++i)
    {
        if (!g_senderClients[i].active || !g_senderClients[i].socket.connected())
        {
            if (g_senderClients[i].active)
            {
                releaseClientSlot(static_cast<int>(i));
            }
            return static_cast<int>(i);
        }
    }

    int oldestUnidentified = -1;
    unsigned long oldestSeen = millis();
    for (size_t i = 0; i < MAX_TCP_CLIENTS; ++i)
    {
        if (g_senderClients[i].identified)
        {
            continue;
        }

        if (oldestUnidentified < 0 || g_senderClients[i].lastSeenMs <= oldestSeen)
        {
            oldestUnidentified = static_cast<int>(i);
            oldestSeen = g_senderClients[i].lastSeenMs;
        }
    }

    if (oldestUnidentified >= 0)
    {
        releaseClientSlot(oldestUnidentified);
        return oldestUnidentified;
    }

    return -1;
}

int findClientByDeviceId(const String &deviceId)
{
    for (size_t i = 0; i < MAX_TCP_CLIENTS; ++i)
    {
        if (!g_senderClients[i].active || !g_senderClients[i].identified)
        {
            continue;
        }

        if (deviceId.equalsIgnoreCase(g_senderClients[i].deviceId))
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

void releaseClientSlot(int index)
{
    if (index < 0 || index >= static_cast<int>(MAX_TCP_CLIENTS))
    {
        return;
    }

    if (g_senderClients[index].socket.connected())
    {
        g_senderClients[index].socket.stop();
    }

    g_senderClients[index] = SenderTcpClient();
}

void bindClientToDeviceId(int slotIndex, const String &deviceId)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(MAX_TCP_CLIENTS))
    {
        return;
    }

    for (int i = 0; i < static_cast<int>(MAX_TCP_CLIENTS); ++i)
    {
        if (i == slotIndex || !g_senderClients[i].active || !g_senderClients[i].identified)
        {
            continue;
        }

        if (deviceId.equalsIgnoreCase(g_senderClients[i].deviceId))
        {
            releaseClientSlot(i);
            break;
        }
    }

    SenderTcpClient &slot = g_senderClients[slotIndex];
    slot.identified = true;
    copyStringToBuffer(deviceId, slot.deviceId, sizeof(slot.deviceId));
}

void markUnknownHeartbeat(const String &deviceId, const WiFiClient &client)
{
    const int unknownIndex = findOrCreateUnknownDevice(deviceId);
    if (unknownIndex < 0)
    {
        return;
    }

    UnknownDevice &device = g_unknownDevices[unknownIndex];
    device.online = true;
    device.lastSeenMs = millis();
    device.seenCount++;
    device.remoteIp = client.remoteIP();
    device.remotePort = client.remotePort();
}

double parseOptionalCoordinate(JsonVariantConst value)
{
    if (value.isNull())
    {
        return NAN;
    }

    if (value.is<double>() || value.is<float>() || value.is<int>() || value.is<long>() || value.is<unsigned long>())
    {
        return value.as<double>();
    }

    return NAN;
}

bool parseTelemetry(JsonObject data, SenderTelemetryPacket &packet)
{
    packet = SenderTelemetryPacket();

    const String deviceId = normalizeDeviceId(String(data["device_id"] | ""));
    if (!isValidDeviceId(deviceId))
    {
        return false;
    }

    copyStringToBuffer(deviceId, packet.deviceId, sizeof(packet.deviceId));
    packet.temperature = data["temperature"] | NAN;
    packet.humidity = data["humidity"] | NAN;
    packet.threshold = data["threshold"] | NAN;
    packet.aiScore = data["ai_score"] | NAN;
    packet.aiAnomaly = String(data["ai_status"] | "").equalsIgnoreCase("ANOMALY");
    packet.latitude = parseOptionalCoordinate(data["latitude"].as<JsonVariantConst>());
    packet.longitude = parseOptionalCoordinate(data["longitude"].as<JsonVariantConst>());
    packet.timestampMs = data["timestamp"] | 0ULL;
    return true;
}

bool parseConfigAck(JsonObject data, SenderConfigAckPacket &packet)
{
    packet = SenderConfigAckPacket();

    const String deviceId = normalizeDeviceId(String(data["device_id"] | ""));
    if (!isValidDeviceId(deviceId))
    {
        return false;
    }

    copyStringToBuffer(deviceId, packet.deviceId, sizeof(packet.deviceId));
    packet.appliedThreshold = data["applied_threshold"] | NAN;
    packet.success = data["success"] | false;
    return true;
}

void applyTelemetry(int slotIndex, const SenderTelemetryPacket &packet, const WiFiClient &client)
{
    const String deviceId(packet.deviceId);
    bindClientToDeviceId(slotIndex, deviceId);

    const int senderIndex = findSenderByDeviceId(deviceId);
    if (senderIndex < 0)
    {
        markUnknownHeartbeat(deviceId, client);
        return;
    }

    SenderState &sender = g_senders[senderIndex];
    sender.online = true;
    sender.pendingDisconnect = false;
    sender.hasTelemetry = true;
    sender.lastSeenMs = millis();
    sender.lastRxNonce = ++g_rxNonce;
    sender.remoteIp = client.remoteIP();
    sender.remotePort = client.remotePort();
    sender.temperature = packet.temperature;
    sender.humidity = packet.humidity;
    const float oldReportedThreshold = sender.reportedThreshold;
    const float oldDesiredThreshold = sender.desiredThreshold;
    sender.reportedThreshold = packet.threshold;
    sender.aiScore = packet.aiScore;
    sender.aiAnomaly = packet.aiAnomaly;
    sender.latitude = packet.latitude;
    sender.longitude = packet.longitude;
    sender.gpsValid = std::isfinite(packet.latitude) && std::isfinite(packet.longitude);
    sender.senderTimestampMs = packet.timestampMs;

    // Khi sender tự đổi threshold cục bộ, receiver cần học lại giá trị mới.
    // Tuy nhiên nếu receiver đang có một phiên cấu hình chờ ACK thì không được
    // ghi đè desiredThreshold để tránh làm hỏng luồng RPC/UI -> receiver -> sender.
    bool persistedThresholdChange = false;
    if (!std::isnan(packet.threshold) && !sender.pendingConfig && !sender.awaitingAck)
    {
        const bool reportedChanged =
            std::isnan(oldReportedThreshold) || fabsf(oldReportedThreshold - packet.threshold) > 0.0001f;
        const bool desiredChanged =
            std::isnan(oldDesiredThreshold) || fabsf(oldDesiredThreshold - packet.threshold) > 0.0001f;

        if (desiredChanged)
        {
            sender.desiredThreshold = packet.threshold;
            persistedThresholdChange = true;
        }

        if (reportedChanged)
        {
            persistedThresholdChange = true;
        }
    }

    if (sender.pendingConfig && !sender.awaitingAck && std::isnan(sender.desiredThreshold))
    {
        sender.desiredThreshold = packet.threshold;
    }

    if (persistedThresholdChange)
    {
        saveSendersFile();
    }
}

void applyConfigAck(int slotIndex, const SenderConfigAckPacket &packet)
{
    bindClientToDeviceId(slotIndex, String(packet.deviceId));
    const int senderIndex = findSenderByDeviceId(String(packet.deviceId));
    if (senderIndex < 0)
    {
        return;
    }

    SenderState &sender = g_senders[senderIndex];
    sender.online = true;
    sender.pendingDisconnect = false;
    sender.lastSeenMs = millis();
    sender.awaitingAck = false;

    if (packet.success)
    {
        sender.pendingConfig = false;
        sender.ackedConfigVersion = sender.configVersion;
        sender.reportedThreshold = packet.appliedThreshold;

        if (sender.pendingRpcId != 0)
        {
            publishRpcResponse(sender.name, sender.pendingRpcId, true, "Da cap nhat threshold", packet.appliedThreshold);
            sender.pendingRpcId = 0;
        }

        saveSendersFile();
        return;
    }

    if (sender.pendingRpcId != 0)
    {
        publishRpcResponse(sender.name, sender.pendingRpcId, false, "Sender tu choi cau hinh");
        sender.pendingRpcId = 0;
    }
}

void processIncomingLine(int slotIndex, WiFiClient &client, const String &line)
{
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok)
    {
        return;
    }

    const String type = String(doc["type"] | "");
    JsonObject data = doc.as<JsonObject>();

    if (type.equalsIgnoreCase("telemetry"))
    {
        SenderTelemetryPacket packet;
        if (parseTelemetry(data, packet))
        {
            applyTelemetry(slotIndex, packet, client);
        }
        return;
    }

    if (type.equalsIgnoreCase("config_ack"))
    {
        SenderConfigAckPacket packet;
        if (parseConfigAck(data, packet))
        {
            applyConfigAck(slotIndex, packet);
        }
    }
}

void serviceClientSlot(int index)
{
    if (index < 0 || index >= static_cast<int>(MAX_TCP_CLIENTS))
    {
        return;
    }

    SenderTcpClient &slot = g_senderClients[index];
    if (!slot.active)
    {
        return;
    }

    if (!slot.socket.connected())
    {
        releaseClientSlot(index);
        return;
    }

    size_t bytesProcessed = 0;
    size_t linesProcessed = 0;
    while (slot.socket.available() > 0 && bytesProcessed < TCP_MAX_BYTES_PER_CYCLE &&
           linesProcessed < TCP_MAX_LINES_PER_CYCLE)
    {
        const int value = slot.socket.read();
        if (value < 0)
        {
            break;
        }

        slot.lastSeenMs = millis();
        bytesProcessed++;
        const char current = static_cast<char>(value);

        if (current == '\r')
        {
            continue;
        }

        if (current == '\n')
        {
            slot.buffer[slot.bufferLen] = '\0';
            if (slot.bufferLen > 0)
            {
                processIncomingLine(index, slot.socket, String(slot.buffer));
                linesProcessed++;
            }
            slot.bufferLen = 0;
            continue;
        }

        if (slot.bufferLen + 1 < sizeof(slot.buffer))
        {
            slot.buffer[slot.bufferLen++] = current;
        }
        else
        {
            slot.bufferLen = 0;
        }
    }
}

} // namespace

void setupSenderTransport()
{
    g_senderServer.begin();
    g_senderServer.setNoDelay(true);
}

bool sendConfigPacket(int index, uint32_t rpcId)
{
    if (index < 0 || index >= static_cast<int>(MAX_SENDERS))
    {
        return false;
    }

    SenderState &sender = g_senders[index];
    if (!sender.used || !sender.registered || !sender.online)
    {
        return false;
    }

    const int clientIndex = findClientByDeviceId(String(sender.deviceId));
    if (clientIndex < 0)
    {
        return false;
    }

    SenderTcpClient &client = g_senderClients[clientIndex];
    if (!client.active || !client.socket.connected())
    {
        return false;
    }

    JsonDocument doc;
    doc["type"] = "config";
    doc["device_id"] = sender.deviceId;
    doc["name"] = sender.name;
    doc["threshold"] = std::isnan(sender.desiredThreshold) ? sender.reportedThreshold : sender.desiredThreshold;
    doc["receiver_ip"] = WiFi.localIP().toString();
    doc["receiver_port"] = LOCAL_TCP_PORT;
    doc["config_version"] = sender.configVersion;

    String payload;
    serializeJson(doc, payload);
    payload += '\n';

    const size_t written = client.socket.print(payload);
    if (written == 0)
    {
        return false;
    }

    sender.pendingConfig = true;
    sender.awaitingAck = true;
    sender.configSentAtMs = millis();
    if (rpcId != 0)
    {
        sender.pendingRpcId = rpcId;
    }

    return true;
}

void processSenderTransport()
{
    while (true)
    {
        WiFiClient newcomer = g_senderServer.available();
        if (!newcomer)
        {
            break;
        }

        const int slot = allocateClientSlot();
        if (slot >= 0)
        {
            g_senderClients[slot] = SenderTcpClient();
            g_senderClients[slot].socket = newcomer;
            g_senderClients[slot].active = true;
            g_senderClients[slot].lastSeenMs = millis();
        }
        else
        {
            newcomer.stop();
        }
    }

    for (int i = 0; i < static_cast<int>(MAX_TCP_CLIENTS); ++i)
    {
        serviceClientSlot(i);
    }
}

} // namespace receiver
