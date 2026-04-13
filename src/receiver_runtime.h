#pragma once

#include "app_state.h"

namespace receiver
{

// ============================================================
//  Runtime điều phối nội bộ
//  Bao gồm Wi-Fi, timeout online/offline, retry cấu hình và
//  các thao tác thêm, sửa, xóa sender từ giao diện web.
// ============================================================

void setupNetworking();
void maintainWifi();
void handleSenderOfflineTransitions();
void processConfigRetries();

bool saveSenderConfigFromUi(const String &deviceId, const String &name, float threshold, String &error);
bool deleteSenderFromUi(const String &deviceId, String &error);

} // namespace receiver
