/**
 * ============================================================
 *  DỰ ÁN: ESP32 SMART AI GATEWAY
 * ============================================================
 *  Mô tả tổng quan:
 *    Hệ thống nhúng tích hợp 4 tính năng chính chạy song song
 *    trên vi điều khiển ESP32-S3 (Yolo Uno board):
 *
 *  1. WEB DASHBOARD (ESPAsyncWebServer + WebSocket + LittleFS)
 *     - ESP32 tự phát WiFi AP "ESP32-Smart-AI-Gateway" để
 *       người dùng truy cập giao diện cấu hình qua trình duyệt.
 *     - Giao tiếp 2 chiều realtime qua WebSocket (không cần
 *       refresh trang).
 *     - Cho phép: cấu hình WiFi/MQTT, chọn màu NeoPixel,
 *       điều chỉnh ngưỡng AI bằng thanh trượt.
 *     - Cấu hình được lưu bền vững vào bộ nhớ flash (LittleFS).
 *
 *  2. COREIOT / THINGSBOARD MQTT
 *     - Kết nối lên Cloud CoreIoT (app.coreiot.io) qua giao
 *       thức MQTT dùng ThingsBoard SDK.
 *     - Gửi Telemetry mỗi giây: nhiệt độ, độ ẩm, trạng thái
 *       AI, màu NeoPixel hiện tại.
 *     - Nhận lệnh RPC từ Dashboard Cloud:
 *         setState(true)  → bật NeoPixel màu Blue
 *         setState(false) → tắt NeoPixel
 *     - LED GPIO48 sáng khi đang kết nối Cloud thành công.
 *
 *  3. TINYML — ANOMALY DETECTION (TensorFlow Lite Micro)
 *     - Chạy mô hình Deep Learning đã được nén (1684 bytes)
 *       trực tiếp trên ESP32, không cần Internet.
 *     - Đầu vào: cặp (Nhiệt độ, Độ ẩm) từ cảm biến DHT20.
 *     - Đầu ra: điểm số bất thường (0.0 → 1.0).
 *     - Phán quyết: score >= threshold → "ANOMALY", ngược lại
 *       → "NORMAL". Ngưỡng mặc định 0.5, có thể chỉnh từ Web.
 *
 *  4. FREERTOS — ĐA NHIỆM
 *     - TaskSensor  (priority 3): Đọc DHT20 mỗi 1 giây.
 *     - TaskAI      (priority 2): Chạy TFLite inference mỗi 1s.
 *     - TaskMQTT    (priority 1): Kết nối + gửi telemetry + RPC.
 *     - loop()                  : WebSocket broadcast + DNS.
 *
 * ============================================================
 *  PHẦN CỨNG:
 *    Board    : ESP32-S3 (Yolo Uno)
 *    DHT20    : Cảm biến nhiệt độ/độ ẩm — I2C SDA=11, SCL=12
 *    NeoPixel : LED RGB 1 bóng           — GPIO 45
 *    LED báo  : LED trạng thái Cloud     — GPIO 48
 *
 *  THƯ VIỆN CẦN CÀI (platformio.ini):
 *    - ESPAsyncWebServer + AsyncTCP
 *    - ArduinoJson
 *    - ThingsBoard + Arduino_MQTT_Client
 *    - Adafruit NeoPixel
 *    - DHT20
 *    - TensorFlowLite_ESP32
 *    - board_build.filesystem = littlefs
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ThingsBoard.h>
#include <Arduino_MQTT_Client.h>
#include <Adafruit_NeoPixel.h>
#include <DHT20.h>

// TensorFlow Lite Micro — thư viện AI chạy trên vi điều khiển
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"		// Tập hợp tất cả phép toán AI
#include "tensorflow/lite/micro/micro_error_reporter.h" // Báo lỗi TFLite
#include "tensorflow/lite/micro/micro_interpreter.h"	// Bộ thực thi mô hình
#include "dht_anomaly_model.h"							// Mảng byte chứa mô hình AI đã được nén (flatbuffer)

// ============================================================
//  CẤU HÌNH PHẦN CỨNG — chỉnh tại đây nếu đổi board/pin
// ============================================================
#define I2C_SDA 11						 // Chân Data I2C của DHT20
#define I2C_SCL 12						 // Chân Clock I2C của DHT20
#define NEO_PIN 45						 // Chân tín hiệu điều khiển NeoPixel
#define GPIO_LED 48						 // LED đơn báo trạng thái kết nối Cloud
#define SSID_AP "ESP32-Smart-AI-Gateway" // Tên WiFi ESP32 phát ra để cấu hình
#define PASS_AP "12345678"				 // Mật khẩu WiFi AP (tối thiểu 8 ký tự)

// ============================================================
//  BIẾN TOÀN CỤC — Dữ liệu chia sẻ giữa các Task FreeRTOS
// ============================================================

// --- Dữ liệu cảm biến (cập nhật bởi TaskSensor) ---
float g_temp = 0.0f; // Nhiệt độ hiện tại (°C)
float g_humi = 0.0f; // Độ ẩm hiện tại (%)

// --- Dữ liệu AI (cập nhật bởi TaskAI) ---
float g_aiScore = 0.0f;				// Điểm số bất thường từ mô hình (0.0 - 1.0)
float g_threshold = 0.5f;			// Ngưỡng phán quyết: >= threshold → ANOMALY
String g_aiStatus = "Initializing"; // Trạng thái hiện tại: "NORMAL" / "ANOMALY"

// --- Trạng thái NeoPixel ---
// Màu chỉ được chọn từ Web Dashboard.
// RPC từ Cloud chỉ được phép bật (Blue) hoặc tắt.
String g_neoColor = "OFF"; // Tên màu hiện tại, gửi lên Cloud làm telemetry
bool g_neoState = false;   // true = đang sáng, false = đang tắt

// --- Cờ điều khiển kết nối Cloud ---
// Web Dashboard bấm "CONNECT SERVER" → true
// Web Dashboard bấm "DISCONNECT"     → false
bool g_serverConnected = false;

// --- Cấu hình mạng (được load từ file /info.dat khi khởi động) ---
String WIFI_SSID = "";				   // SSID mạng WiFi muốn kết nối
String WIFI_PASS = "";				   // Mật khẩu WiFi
String MQTT_SERVER = "app.coreiot.io"; // Địa chỉ MQTT broker CoreIoT
String MQTT_PORT = "1883";			   // Cổng MQTT (mặc định 1883)
String DEVICE_TOKEN = "";			   // Token xác thực thiết bị trên CoreIoT

// ============================================================
//  KHỞI TẠO ĐỐI TƯỢNG PHẦN CỨNG & MẠNG
// ============================================================
Adafruit_NeoPixel strip(1, NEO_PIN, NEO_GRB + NEO_KHZ800); // 1 LED NeoPixel RGB
DHT20 dht20;											   // Cảm biến nhiệt độ/độ ẩm giao tiếp I2C
DNSServer dnsServer;									   // DNS server cho Captive Portal (AP mode)
AsyncWebServer webServer(80);							   // HTTP Web Server lắng nghe cổng 80
AsyncWebSocket ws("/ws");								   // WebSocket endpoint tại đường dẫn /ws
WiFiClient wifiClient;									   // TCP client dùng cho kết nối MQTT
Arduino_MQTT_Client mqttClient(wifiClient);				   // Wrapper MQTT tương thích ThingsBoard
ThingsBoard tb(mqttClient, 256);						   // SDK ThingsBoard, buffer JSON 256 bytes

// ============================================================
//  TFLITE NAMESPACE
//  Tất cả biến nội bộ của TFLite đặt trong namespace để tránh
//  xung đột tên với phần còn lại của chương trình.
// ============================================================
namespace
{
	// Đối tượng báo lỗi — in thông tin lỗi ra Serial
	tflite::ErrorReporter *error_reporter = nullptr;

	// Con trỏ tới mô hình AI đã được nạp từ mảng byte
	const tflite::Model *model = nullptr;

	// Bộ thực thi mô hình — thực hiện các phép tính toán học
	tflite::MicroInterpreter *interpreter = nullptr;

	// Tensor đầu vào: nhận [nhiệt độ, độ ẩm]
	TfLiteTensor *tfl_input = nullptr;

	// Tensor đầu ra: trả về điểm số bất thường [0.0 - 1.0]
	TfLiteTensor *tfl_output = nullptr;

	// Vùng nhớ "nháp" để TFLite thực hiện tính toán trung gian.
	// 8KB đủ cho mô hình nhỏ dạng Dense Neural Network.
	constexpr int kTensorArenaSize = 8 * 1024;
	uint8_t tensor_arena[kTensorArenaSize];
}

// ============================================================
//  LITTLEFS — LƯU TRỮ CẤU HÌNH BỀN VỮNG
//  File /info.dat lưu dạng JSON, tồn tại ngay cả khi mất điện.
// ============================================================

/**
 * Load_info_File()
 * Đọc file /info.dat và nạp cấu hình vào các biến toàn cục.
 * Nếu file chưa tồn tại (lần đầu chạy), bỏ qua và giữ giá trị mặc định.
 */
void Load_info_File()
{
	if (!LittleFS.exists("/info.dat"))
		return;
	File f = LittleFS.open("/info.dat", "r");
	if (!f)
		return;
	DynamicJsonDocument doc(512);
	deserializeJson(doc, f);
	f.close();
	WIFI_SSID = doc["ssid"] | "";
	WIFI_PASS = doc["pass"] | "";
	DEVICE_TOKEN = doc["token"] | "";
	MQTT_SERVER = doc["server"] | "app.coreiot.io";
	MQTT_PORT = doc["port"] | "1883";
	g_threshold = doc["threshold"] | 0.5f;
	Serial.println("[FS] Config loaded.");
}

/**
 * Save_info_File()
 * Ghi cấu hình vào file /info.dat dạng JSON.
 *
 * @param s         SSID WiFi mới (truyền "" để giữ giá trị cũ)
 * @param p         Password WiFi mới
 * @param t         Device Token CoreIoT mới
 * @param srv       MQTT Server address mới
 * @param prt       MQTT Port mới
 * @param threshold Ngưỡng AI mới
 * @param reboot    true  → restart ESP32 sau khi lưu (áp dụng WiFi mới)
 *                  false → chỉ lưu, KHÔNG restart (dùng khi chỉ đổi threshold)
 */
void Save_info_File(String s, String p, String t, String srv, String prt,
					float threshold, bool reboot = true)
{
	DynamicJsonDocument doc(512);
	// Nếu tham số truyền vào rỗng → giữ nguyên giá trị cũ đang có
	doc["ssid"] = (s == "") ? WIFI_SSID : s;
	doc["pass"] = (p == "") ? WIFI_PASS : p;
	doc["token"] = (t == "") ? DEVICE_TOKEN : t;
	doc["server"] = (srv == "") ? MQTT_SERVER : srv;
	doc["port"] = (prt == "") ? MQTT_PORT : prt;
	doc["threshold"] = threshold;
	File f = LittleFS.open("/info.dat", "w");
	if (f)
	{
		serializeJson(doc, f);
		f.close();
	}
	Serial.println("[FS] Config saved.");
	if (reboot)
	{
		delay(500);
		ESP.restart();
	}
}

// ============================================================
//  RPC CALLBACK — Nhận lệnh điều khiển từ Cloud Dashboard
//
//  ThingsBoard gửi lệnh RPC xuống thiết bị qua MQTT topic:
//    v1/devices/me/rpc/request/{id}
//  SDK tự parse và gọi hàm callback tương ứng với "method".
//
//  Lưu ý thiết kế: RPC CHỈ được phép bật/tắt NeoPixel.
//  Màu sắc do người dùng chọn tại Web Dashboard (setNeo).
//  Khi RPC bật (setState=true) → luôn hiển thị màu Blue.
// ============================================================

/**
 * rpcSetState()
 * Callback xử lý lệnh "setState" từ Cloud.
 * @param data  Giá trị boolean: true = bật, false = tắt
 * @return      RPC_Response phản hồi lại Cloud xác nhận trạng thái mới
 */
RPC_Response rpcSetState(const RPC_Data &data)
{
	g_neoState = data;
	if (g_neoState)
	{
		// Bật: màu Blue cố định — Cloud không được chọn màu khác
		strip.setPixelColor(0, strip.Color(0, 0, 255));
		g_neoColor = "Blue";
	}
	else
	{
		// Tắt: tắt hoàn toàn LED
		strip.setPixelColor(0, 0);
		g_neoColor = "OFF";
	}
	strip.show();
	Serial.printf("[RPC] setState -> %s\n", g_neoState ? "ON (Blue)" : "OFF");
	// Trả về plain boolean (không có key) để CoreIoT Switch widget đọc được
	StaticJsonDocument<8> doc;
	doc.set(g_neoState);
	return RPC_Response(doc.as<JsonVariant>());
}

/**
 * rpcGetState()
 * Callback xử lý lệnh "getState" từ Cloud.
 * Dùng để Dashboard đồng bộ trạng thái switch khi mới load.
 *
 * CoreIoT Switch widget yêu cầu response là plain boolean (true/false),
 * KHÔNG phải dạng key-value {"getState": true}.
 * Dùng StaticJsonDocument để tạo JsonVariant chứa boolean thuần túy.
 *
 * @return  RPC_Response chứa boolean: true = đang sáng, false = đang tắt
 */
RPC_Response rpcGetState(const RPC_Data &data)
{
	// Tạo JSON document chứa plain boolean, không có key
	StaticJsonDocument<8> doc;
	doc.set(g_neoState); // serialize thành: true hoặc false (không có key)
	return RPC_Response(doc.as<JsonVariant>());
}

// Khai báo từng RPC callback riêng lẻ.
// Lưu ý: THINGSBOARD_ENABLE_STL đang bật nên KHÔNG dùng
// overload RPC_Subscribe(array, size) — phải subscribe từng cái.
RPC_Callback cb_setState("setState", rpcSetState);
RPC_Callback cb_getState("getState", rpcGetState);

// ============================================================
//  WEBSOCKET HANDLER — Nhận lệnh từ Web Dashboard
//
//  Tất cả lệnh từ trình duyệt đều gửi qua WebSocket dạng JSON:
//    { "action": "<tên lệnh>", ...tham số... }
//
//  Các action được hỗ trợ:
//    "setNeo"       → Chọn màu NeoPixel (r, g, b, name)
//    "setServer"    → Bật/tắt kết nối Cloud MQTT
//    "saveConfig"   → Lưu cấu hình WiFi/MQTT và restart
//    "setThreshold" → Cập nhật ngưỡng AI (không restart)
// ============================================================

/**
 * handleWSMessage()
 * Phân tích gói JSON nhận từ trình duyệt và thực thi lệnh.
 * @param arg  Thông tin frame WebSocket (không dùng trực tiếp)
 * @param data Con trỏ tới dữ liệu JSON thô
 * @param len  Độ dài dữ liệu (byte)
 */
void handleWSMessage(void *arg, uint8_t *data, size_t len)
{
	DynamicJsonDocument doc(512);
	deserializeJson(doc, data, len);
	String action = doc["action"] | "";

	// --- Chọn màu NeoPixel từ bảng màu trên Web ---
	// Web gửi: { "action":"setNeo", "r":255, "g":0, "b":0, "name":"Red" }
	if (action == "setNeo")
	{
		uint8_t r = doc["r"] | 0;
		uint8_t g = doc["g"] | 0;
		uint8_t b = doc["b"] | 0;
		g_neoColor = doc["name"] | "Custom";
		g_neoState = (r || g || b); // Nếu màu = 0,0,0 thì coi như đang tắt
		strip.setPixelColor(0, strip.Color(r, g, b));
		strip.show();
	}

	// --- Bật/tắt kết nối Cloud MQTT ---
	// Web gửi: { "action":"setServer", "status":"CONNECT" }
	//      hoặc { "action":"setServer", "status":"DISCONNECT" }
	else if (action == "setServer")
	{
		g_serverConnected = (String(doc["status"] | "") == "CONNECT");
		if (!g_serverConnected)
		{
			if (tb.connected())
				tb.disconnect();
			digitalWrite(GPIO_LED, LOW); // Tắt LED ngay lập tức khi user bấm Disconnect
			Serial.println("[MQTT] Disconnected by user.");
		}
	}

	// --- Lưu toàn bộ cấu hình và restart thiết bị ---
	// Web gửi: { "action":"saveConfig", "ssid":"...", "pass":"...",
	//            "token":"...", "server":"...", "port":"...", "threshold":0.5 }
	else if (action == "saveConfig")
	{
		Save_info_File(
			doc["ssid"] | "",
			doc["pass"] | "",
			doc["token"] | "",
			doc["server"] | "",
			doc["port"] | "",
			doc["threshold"] | g_threshold,
			true // restart sau khi lưu
		);
	}

	// --- Chỉ cập nhật ngưỡng AI, KHÔNG restart ---
	// Được gọi khi người dùng nhả thanh trượt threshold trên Web.
	// Web gửi: { "action":"setThreshold", "threshold":0.7 }
	else if (action == "setThreshold")
	{
		g_threshold = doc["threshold"] | 0.5f;
		// Lưu vào file để giữ giá trị sau khi restart
		Save_info_File(WIFI_SSID, WIFI_PASS, DEVICE_TOKEN,
					   MQTT_SERVER, MQTT_PORT, g_threshold, false);
		Serial.printf("[AI] Threshold -> %.2f\n", g_threshold);
	}
}

/**
 * onWsEvent()
 * Callback xử lý tất cả sự kiện WebSocket:
 *   WS_EVT_CONNECT    → Client mới kết nối: gửi cấu hình hiện tại
 *   WS_EVT_DISCONNECT → Client ngắt kết nối
 *   WS_EVT_DATA       → Nhận dữ liệu từ client: chuyển sang handleWSMessage()
 */
void onWsEvent(AsyncWebSocket *s, AsyncWebSocketClient *c,
			   AwsEventType t, void *arg, uint8_t *d, size_t l)
{
	if (t == WS_EVT_DATA)
	{
		handleWSMessage(arg, d, l);
	}
	else if (t == WS_EVT_CONNECT)
	{
		// Khi trình duyệt vừa mở Web Dashboard, gửi ngay cấu hình
		// hiện tại để điền sẵn vào tất cả các ô input trên trang.
		DynamicJsonDocument cfg(512);
		cfg["type"] = "config";
		cfg["ssid"] = WIFI_SSID;
		cfg["pass"] = WIFI_PASS;
		cfg["server"] = MQTT_SERVER;
		cfg["port"] = MQTT_PORT;
		cfg["token"] = DEVICE_TOKEN;
		cfg["threshold"] = g_threshold;
		String out;
		serializeJson(cfg, out);
		c->text(out);
	}
}

// ============================================================
//  FREERTOS TASK 1 — TaskSensor
//
//  Đọc dữ liệu từ cảm biến DHT20 mỗi 1 giây.
//  Chạy ở priority cao nhất (3) để đảm bảo dữ liệu cảm biến
//  luôn được cập nhật đúng thời gian, không bị task khác block.
//
//  DHT20 giao tiếp qua I2C:
//    dht20.read()           → kích hoạt đo, trả 0 nếu OK
//    dht20.getTemperature() → nhiệt độ °C
//    dht20.getHumidity()    → độ ẩm %RH
// ============================================================
void TaskSensor(void *p)
{
	Wire.begin(I2C_SDA, I2C_SCL); // Khởi tạo bus I2C cho task này
	dht20.begin();
	while (1)
	{
		dht20.read();
		g_temp = dht20.getTemperature();
		g_humi = dht20.getHumidity();
		vTaskDelay(pdMS_TO_TICKS(1000)); // Nghỉ 1 giây trước lần đo tiếp theo
	}
}

// ============================================================
//  FREERTOS TASK 2 — TaskAI
//
//  Chạy mô hình TensorFlow Lite Micro để phát hiện bất thường.
//
//  Quy trình inference mỗi giây:
//    1. Ghi (g_temp, g_humi) vào tensor đầu vào
//    2. Gọi interpreter->Invoke() để chạy mô hình
//    3. Đọc điểm số từ tensor đầu ra (0.0 - 1.0)
//    4. So sánh với g_threshold → cập nhật g_aiStatus
//
//  Kiến trúc mô hình: Input(2) → Dense(8, ReLU) → Dense(1, Sigmoid)
//  Score gần 1.0 = bất thường, gần 0.0 = bình thường.
// ============================================================
void TaskAI(void *p)
{
	// --- Bước 1: Nạp mô hình từ mảng byte trong dht_anomaly_model.h ---
	static tflite::MicroErrorReporter micro_reporter;
	error_reporter = &micro_reporter;
	model = tflite::GetModel(dht_anomaly_model_tflite);

	// --- Bước 2: Tạo Resolver chứa tất cả phép toán mô hình cần ---
	static tflite::AllOpsResolver resolver;

	// --- Bước 3: Tạo Interpreter với vùng nhớ tensor_arena ---
	static tflite::MicroInterpreter static_interpreter(
		model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
	interpreter = &static_interpreter;

	// --- Bước 4: Cấp phát bộ nhớ cho tất cả tensor trong mô hình ---
	interpreter->AllocateTensors();

	// --- Bước 5: Lấy con trỏ tới tensor input/output để dùng sau ---
	tfl_input = interpreter->input(0);
	tfl_output = interpreter->output(0);
	Serial.println("[TFLite] Ready.");

	while (1)
	{
		// Đưa dữ liệu cảm biến vào tensor đầu vào
		tfl_input->data.f[0] = g_temp; // index 0: nhiệt độ
		tfl_input->data.f[1] = g_humi; // index 1: độ ẩm

		// Chạy inference — mô hình tính toán và ghi kết quả vào output tensor
		if (interpreter->Invoke() == kTfLiteOk)
		{
			g_aiScore = tfl_output->data.f[0];
			g_aiStatus = (g_aiScore >= g_threshold) ? "ANOMALY" : "NORMAL";
		}
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

// ============================================================
//  FREERTOS TASK 3 — TaskMQTT
//
//  Quản lý toàn bộ vòng đời kết nối MQTT với CoreIoT Cloud.
//
//  Luồng hoạt động:
//    - Chờ g_serverConnected = true (người dùng bấm CONNECT)
//    - Nếu chưa kết nối → thử tb.connect() với token
//    - Sau khi kết nối → đăng ký 2 RPC callback (setState, getState)
//    - Gửi 4 telemetry mỗi giây: temperature, humidity, ai_status, neo_color
//    - Gọi tb.loop() để duy trì keepalive và nhận tin RPC từ Cloud
//    - Nếu g_serverConnected = false → ngắt kết nối, tắt LED báo
//
//  ThingsBoard SDK tự xử lý reconnect nội bộ nếu mạng chập chờn.
// ============================================================
void TaskMQTT(void *p)
{
	while (1)
	{
		// --- Trường hợp 1: Được yêu cầu kết nối và WiFi đang sẵn sàng ---
		if (g_serverConnected && WiFi.status() == WL_CONNECTED)
		{
			if (!tb.connected())
			{
				Serial.println("[MQTT] Connecting ThingsBoard...");
				if (tb.connect(MQTT_SERVER.c_str(), DEVICE_TOKEN.c_str(),
							   MQTT_PORT.toInt()))
				{
					// Đăng ký RPC callback ngay sau khi kết nối thành công
					// (phải đăng ký lại mỗi lần reconnect)
					tb.RPC_Subscribe(cb_setState);
					tb.RPC_Subscribe(cb_getState);
					Serial.println("[MQTT] Connected!");
					digitalWrite(GPIO_LED, HIGH); // Bật LED báo Online
				}
			}

			if (tb.connected())
			{
				// Gửi telemetry lên Cloud (4 field mỗi lần)
				tb.sendTelemetryData("temperature", g_temp);
				tb.sendTelemetryData("humidity", g_humi);
				tb.sendTelemetryData("ai_status", g_aiStatus.c_str());
				tb.sendTelemetryData("neo_color", g_neoColor.c_str());

				// QUAN TRỌNG: tb.loop() phải được gọi thường xuyên để:
				//   - Duy trì kết nối MQTT (keepalive ping)
				//   - Nhận và xử lý gói tin RPC từ Cloud
				tb.loop();
			}
		}

		// --- Trường hợp 2: Được yêu cầu ngắt kết nối ---
		else if (!g_serverConnected && tb.connected())
		{
			tb.disconnect();
			digitalWrite(GPIO_LED, LOW); // Tắt LED báo Offline
			Serial.println("[MQTT] Disconnected.");
		}

		vTaskDelay(pdMS_TO_TICKS(1000)); // Chu kỳ 1 giây
	}
}

// ============================================================
//  SETUP — Chạy một lần khi ESP32 khởi động
// ============================================================
void setup()
{
	Serial.begin(115200);

	// --- Khởi tạo phần cứng ---
	pinMode(GPIO_LED, OUTPUT);
	digitalWrite(GPIO_LED, LOW); // Tắt LED báo ban đầu

	strip.begin();
	strip.setBrightness(50);   // Độ sáng 50/255 (~20%) tránh quá chói
	strip.setPixelColor(0, 0); // Tắt NeoPixel
	strip.show();

	// --- Khởi động LittleFS và load cấu hình ---
	if (!LittleFS.begin(true)) // true = format nếu mount thất bại
		Serial.println("[FS] LittleFS Fail!");
	Load_info_File();

	// --- Cấu hình WiFi chế độ AP+STA song song ---
	// AP: Luôn phát WiFi "ESP32-Smart-AI-Gateway" để người dùng
	//     có thể vào cấu hình bất kỳ lúc nào, kể cả khi đã có WiFi.
	// STA: Kết nối vào router nếu đã có SSID được lưu.
	WiFi.mode(WIFI_AP_STA);
	WiFi.softAP(SSID_AP, PASS_AP);

	// DNS server redirect toàn bộ domain về 192.168.4.1 (Captive Portal)
	// → Khi kết nối AP, mở trình duyệt bất kỳ URL nào cũng vào được Web
	dnsServer.start(53, "*", WiFi.softAPIP());
	Serial.printf("[WiFi] AP: %s  IP: %s\n", SSID_AP,
				  WiFi.softAPIP().toString().c_str());
	if (WIFI_SSID != "")
		WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());

	// --- Cấu hình Web Server ---
	ws.onEvent(onWsEvent);	   // Đăng ký handler WebSocket
	webServer.addHandler(&ws); // Gắn WebSocket vào server

	// Phục vụ tất cả file tĩnh (index.html, script.js, styles.css)
	// từ thư mục gốc của LittleFS
	webServer.serveStatic("/", LittleFS, "/");

	// Route mặc định → trả về index.html
	webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *r)
				 { r->send(LittleFS, "/index.html", "text/html"); });

	// 404: redirect về trang chủ (hỗ trợ Captive Portal)
	webServer.onNotFound([](AsyncWebServerRequest *r)
						 { r->redirect("/"); });

	webServer.begin();
	Serial.println("[WEB] Server started.");

	// --- Khởi tạo FreeRTOS Tasks ---
	// Tham số xTaskCreate: (hàm, tên, stack_size, param, priority, handle)
	xTaskCreate(TaskSensor, "Sensor", 4096, NULL, 3, NULL); // Đọc DHT20
	xTaskCreate(TaskAI, "AI", 10240, NULL, 2, NULL);		// TFLite inference
	xTaskCreate(TaskMQTT, "MQTT", 8192, NULL, 1, NULL);		// Cloud MQTT
}

// ============================================================
//  LOOP — Chạy liên tục trên Core 1 (core mặc định của Arduino)
//
//  Hai nhiệm vụ chính:
//    1. Xử lý DNS request (Captive Portal cho AP mode)
//    2. Broadcast dữ liệu realtime lên tất cả client WebSocket
//       mỗi 1 giây — Web Dashboard dùng để cập nhật giao diện.
//
//  Gói tin JSON gửi đi:
//    { "type":"update", "temp":28.5, "humi":65.2,
//      "neo":"Blue", "ai":"NORMAL", "score":0.12,
//      "threshold":0.5, "serverStatus":"Connected" }
// ============================================================
void loop()
{
	// Xử lý DNS cho Captive Portal — phải gọi mỗi vòng loop
	dnsServer.processNextRequest();

	static unsigned long lastUpdate = 0;
	if (millis() - lastUpdate > 1000)
	{
		lastUpdate = millis();
		ws.cleanupClients(); // Giải phóng các kết nối WebSocket đã đóng

		if (ws.count() > 0)
		{ // Chỉ gửi khi có ít nhất 1 client đang mở Web
			DynamicJsonDocument doc(256);
			doc["type"] = "update";
			doc["temp"] = g_temp;
			doc["humi"] = g_humi;
			doc["neo"] = g_neoColor;
			doc["ai"] = g_aiStatus;
			doc["score"] = g_aiScore;
			doc["threshold"] = g_threshold;
			doc["serverStatus"] = tb.connected() ? "Connected" : "Disconnected";
			String out;
			serializeJson(doc, out);
			ws.textAll(out); // Broadcast tới TẤT CẢ client đang kết nối
		}
	}

	// Nhường CPU cho hệ thống WiFi/BT stack xử lý
	// Không được delay(0) hoặc bỏ trống vì sẽ làm watchdog reset
	vTaskDelay(pdMS_TO_TICKS(10));
}