#include "app_state.h"
#include "cloud_gateway.h"
#include "sender_transport.h"
#include "receiver_runtime.h"
#include "storage.h"
#include "web_ui.h"

using namespace receiver;

// ============================================================
//  Main chỉ còn vai trò điều phối
//  Receiver giờ hoạt động theo mô hình Wi-Fi nội bộ + TCP:
//  - sender mở TCP client tới receiver
//  - receiver phân loại known / unknown theo device_id = MAC
//  - sender đã đăng ký mới được xử lý telemetry và gửi CoreIoT
// ============================================================

void setup()
{
	Serial.begin(115200);
	delay(200);

	if (!LittleFS.begin(true))
	{
		Serial.println("[FS] Mount LittleFS thất bại");
	}

	loadReceiverConfig();
	loadSendersFile();
	setupNetworking();
	setupSenderTransport();
	startWebServer();
}

void loop()
{
	g_dnsServer.processNextRequest();
	g_ws.cleanupClients();

	processSenderTransport();
	maintainWifi();
	handleSenderOfflineTransitions();
	processConfigRetries();
	maintainMqtt();

	if (millis() - g_lastSnapshotAt >= SNAPSHOT_INTERVAL_MS)
	{
		g_lastSnapshotAt = millis();
		if (g_ws.count() > 0)
		{
			pushSnapshot();
		}
	}

	delay(10);
}
