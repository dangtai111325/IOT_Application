#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

#include "app_state.h"
#include "storage.h"
#include "simulator.h"
#include "ai_engine.h"
#include "sender_transport.h"
#include "web_ui.h"

namespace sender
{

    // ============================================================
    //  Khởi tạo toàn bộ sender
    // ============================================================

    void appSetup()
    {
        Serial.begin(115200);
        delay(200);
        randomSeed(micros());

        if (!LittleFS.begin(true))
        {
            Serial.println("Khởi tạo LittleFS thất bại.");
        }

        loadConfig();

        WiFi.mode(WIFI_AP_STA);
        g_deviceId = normalizeMac(WiFi.macAddress());
        g_apSsid = "Sender-" + g_deviceId.substring(g_deviceId.length() - 8);
        g_apSsid.replace(":", "");

        setupSoftAp();
        maintainWifiSta();
        setupAiEngine();
        startWebServer();
        markReceiverStatus();
    }

    // ============================================================
    //  Vòng lặp chính: mô phỏng sensor, chạy AI, giữ mạng và UI
    // ============================================================

    void appLoop()
    {
        updateSimulatedEnvironment();
        runAiInference();
        maintainWifiSta();
        maintainTcp();

        if (millis() - g_lastSnapshotAt >= SNAPSHOT_INTERVAL_MS)
        {
            g_lastSnapshotAt = millis();
            pushSnapshot();
        }

        delay(10);
    }

} // namespace sender

void setup()
{
    sender::appSetup();
}

void loop()
{
    sender::appLoop();
}
