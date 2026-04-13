#pragma once

#include "app_state.h"

namespace receiver
{

// ============================================================
//  Liên kết sender qua TCP nội bộ
//  Sender và receiver cùng mạng Wi-Fi. Sender chủ động mở
//  TCP client tới receiver và gửi từng dòng JSON kết thúc bằng \n.
// ============================================================

void setupSenderTransport();
void processSenderTransport();
bool sendConfigPacket(int index, uint32_t rpcId);

} // namespace receiver
