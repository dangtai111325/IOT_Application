#include "shared_utils.h"

namespace receiver
{

namespace
{

bool deviceIdEquals(const char *stored, const String &deviceId)
{
    return deviceId.equalsIgnoreCase(stored);
}

} // namespace

void copyStringToBuffer(const String &value, char *buffer, size_t size)
{
    if (buffer == nullptr || size == 0)
    {
        return;
    }

    value.substring(0, size - 1).toCharArray(buffer, size);
    buffer[size - 1] = '\0';
}

String trimName(const String &value)
{
    String out = value;
    out.trim();
    return out;
}

String normalizeDeviceId(const String &value)
{
    String out = trimName(value);
    out.toUpperCase();
    return out;
}

bool isValidDeviceId(const String &value)
{
    if (value.length() != DEVICE_ID_LEN - 1)
    {
        return false;
    }

    for (int i = 0; i < value.length(); ++i)
    {
        const char current = value[i];
        const bool isHex = (current >= '0' && current <= '9') || (current >= 'A' && current <= 'F');
        const bool isSeparator = current == ':';

        if ((i + 1) % 3 == 0)
        {
            if (!isSeparator)
            {
                return false;
            }
        }
        else if (!isHex)
        {
            return false;
        }
    }

    return true;
}

bool isKnownEpochMs(uint64_t value)
{
    return value > 1000000000000ULL;
}

uint8_t currentWifiChannel()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        const int channel = WiFi.channel();
        return channel > 0 ? static_cast<uint8_t>(channel) : 1;
    }

    const int apChannel = WiFi.channel();
    return apChannel > 0 ? static_cast<uint8_t>(apChannel) : 1;
}

const char *senderDisplayName(const SenderState &sender)
{
    return (sender.registered && sender.name[0] != '\0') ? sender.name : "Undefined";
}

int findSenderByDeviceId(const String &deviceId)
{
    if (deviceId.isEmpty())
    {
        return -1;
    }

    for (size_t i = 0; i < MAX_SENDERS; ++i)
    {
        if (!g_senders[i].used)
        {
            continue;
        }

        if (deviceIdEquals(g_senders[i].deviceId, deviceId))
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

int findSenderByName(const String &name)
{
    if (name.isEmpty())
    {
        return -1;
    }

    for (size_t i = 0; i < MAX_SENDERS; ++i)
    {
        if (!g_senders[i].used || !g_senders[i].registered)
        {
            continue;
        }

        if (name.equalsIgnoreCase(g_senders[i].name))
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

int findFreeSenderSlot()
{
    for (size_t i = 0; i < MAX_SENDERS; ++i)
    {
        if (!g_senders[i].used)
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

int findUnknownByDeviceId(const String &deviceId)
{
    if (deviceId.isEmpty())
    {
        return -1;
    }

    for (size_t i = 0; i < MAX_UNKNOWN_DEVICES; ++i)
    {
        if (!g_unknownDevices[i].used)
        {
            continue;
        }

        if (deviceIdEquals(g_unknownDevices[i].deviceId, deviceId))
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

int findFreeUnknownSlot()
{
    for (size_t i = 0; i < MAX_UNKNOWN_DEVICES; ++i)
    {
        if (!g_unknownDevices[i].used)
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

int findOrCreateUnknownDevice(const String &deviceId)
{
    int existing = findUnknownByDeviceId(deviceId);
    if (existing >= 0)
    {
        return existing;
    }

    const int freeSlot = findFreeUnknownSlot();
    if (freeSlot < 0)
    {
        return -1;
    }

    UnknownDevice &device = g_unknownDevices[freeSlot];
    device = UnknownDevice();
    device.used = true;
    copyStringToBuffer(deviceId, device.deviceId, sizeof(device.deviceId));
    return freeSlot;
}

void removeUnknownDevice(const String &deviceId)
{
    const int index = findUnknownByDeviceId(deviceId);
    if (index >= 0)
    {
        g_unknownDevices[index] = UnknownDevice();
    }
}

bool isNameUnique(const String &name, int ignoreIndex)
{
    if (name.isEmpty())
    {
        return false;
    }

    for (size_t i = 0; i < MAX_SENDERS; ++i)
    {
        if (!g_senders[i].used || !g_senders[i].registered || static_cast<int>(i) == ignoreIndex)
        {
            continue;
        }

        if (name.equalsIgnoreCase(g_senders[i].name))
        {
            return false;
        }
    }

    return true;
}

void resetCloudSessionFlags()
{
    for (SenderState &sender : g_senders)
    {
        sender.cloudConnected = false;
        sender.lastUploadedRxNonce = 0;
        sender.publishedThresholdAttribute = NAN;
        sender.publishedReceiverIp = IPAddress();
    }
}

} // namespace receiver
