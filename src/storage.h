#pragma once

#include "app_state.h"

namespace receiver
{

// ============================================================
//  Lưu trữ cấu hình
//  Module này chịu trách nhiệm đọc/ghi LittleFS cho receiver và sender.
// ============================================================

void loadReceiverConfig();
void saveReceiverConfig(bool rebootAfterSave);
void loadSendersFile();
void saveSendersFile();

} // namespace receiver
