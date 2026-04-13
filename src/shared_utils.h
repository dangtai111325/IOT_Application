#pragma once

#include "app_state.h"

namespace receiver
{

// ============================================================
//  Tiện ích và tra cứu trạng thái thiết bị
//  Module này gom các hàm nhỏ dùng chung: chuẩn hóa device_id,
//  tra cứu sender đã đăng ký, tra cứu thiết bị lạ và kiểm tra tên.
// ============================================================

void copyStringToBuffer(const String &value, char *buffer, size_t size);
String trimName(const String &value);
String normalizeDeviceId(const String &value);
bool isValidDeviceId(const String &value);
bool isKnownEpochMs(uint64_t value);
uint8_t currentWifiChannel();
const char *senderDisplayName(const SenderState &sender);

int findSenderByDeviceId(const String &deviceId);
int findSenderByName(const String &name);
int findFreeSenderSlot();

int findUnknownByDeviceId(const String &deviceId);
int findFreeUnknownSlot();
int findOrCreateUnknownDevice(const String &deviceId);
void removeUnknownDevice(const String &deviceId);

bool isNameUnique(const String &name, int ignoreIndex);
void resetCloudSessionFlags();

} // namespace receiver
