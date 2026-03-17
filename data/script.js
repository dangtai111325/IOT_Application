/**
 * BIẾN TOÀN CỤC
 */
let ws;
let isSrvConnected = false;
let connectTimeout;
let thrSaveTimer = null; // Debounce khi kéo thanh trượt threshold
let thrDragging  = false; // Đang kéo slider → không cho server ghi đè

/**
 * HÀM KHỞI TẠO: Thiết lập kết nối WebSocket
 */
function init() {
    ws = new WebSocket(`ws://${window.location.hostname}/ws`);

    ws.onopen = () => {
        document.getElementById('wsStat').innerText = "ĐÃ KẾT NỐI THIẾT BỊ";
        document.getElementById('wsStat').style.color = "green";
        document.getElementById('wsDot').style.backgroundColor = "green";
    };

    ws.onmessage = (e) => {
        let d = JSON.parse(e.data);

        // --- Gói "update": dữ liệu realtime từ loop() ---
        if (d.type === "update") {
            // Sensor
            document.getElementById('temp').innerText = d.temp.toFixed(1) + "°C";
            document.getElementById('humi').innerText = d.humi.toFixed(1) + "%";

            // NeoPixel
            document.getElementById('curColor').innerText = d.neo || "OFF";

            // AI Status
            updateAI(d.score, d.ai, d.threshold);

            // Nút Connect Server
            const btn = document.getElementById('btnServer');
            if (d.serverStatus === "Connected") {
                clearTimeout(connectTimeout);
                btn.innerText = "DISCONNECT SERVER";
                btn.classList.add('connected');
                btn.style.background = "";
                isSrvConnected = true;
            } else {
                if (btn.innerText !== "CONNECTING...") {
                    btn.innerText = "CONNECT SERVER";
                    btn.classList.remove('connected');
                    btn.style.background = "";
                    isSrvConnected = false;
                }
            }
        }

        // --- Gói "config": điền sẵn vào các ô input khi vừa kết nối ---
        else if (d.type === "config") {
            document.getElementById('ssid').value   = d.ssid   || "";
            document.getElementById('pass').value   = d.pass   || "";
            document.getElementById('server').value = d.server || "";
            document.getElementById('port').value   = d.port   || "1883";
            document.getElementById('token').value  = d.token  || "";

            // Đồng bộ thanh trượt threshold — chỉ khi không đang kéo
            if (!thrDragging && d.threshold != null) setSlider(d.threshold);

            // [SỬA] Đồng bộ trạng thái nút CONNECT/DISCONNECT từ firmware
            // Không gửi lệnh gì — chỉ cập nhật UI cho đúng trạng thái thật
            if (d.serverConnected != null) {
                const btn = document.getElementById('btnServer');
                isSrvConnected = !!d.serverConnected;
                if (isSrvConnected) {
                    btn.innerText = "DISCONNECT SERVER";
                    btn.classList.add('connected');
                    btn.style.background = "";
                } else {
                    btn.innerText = "CONNECT SERVER";
                    btn.classList.remove('connected');
                    btn.style.background = "";
                }
            }
        }
    };

    ws.onclose = () => {
        document.getElementById('wsStat').innerText = "MẤT KẾT NỐI THIẾT BỊ";
        document.getElementById('wsStat').style.color = "red";
        document.getElementById('wsDot').style.backgroundColor = "red";
        document.getElementById('temp').innerText = "--";
        document.getElementById('humi').innerText = "--";
        setTimeout(init, 2000);
    };
}

/**
 * CẬP NHẬT GIAO DIỆN AI
 */
function updateAI(score, status, threshold) {
    if (score == null) return;

    const thr = (threshold != null) ? threshold : 0.5;

    // Số điểm
    document.getElementById('aiScore').innerText = score.toFixed(4);

    // Badge trạng thái
    const badge = document.getElementById('aiLabel');
    badge.innerText   = status || "NORMAL";
    badge.className   = 'ai-badge ' + (status === 'ANOMALY' ? 'anomaly' : 'normal');

    // Thanh điểm số
    const bar = document.getElementById('aiBar');
    bar.style.width     = Math.min(score * 100, 100) + '%';
    bar.className       = 'bar-fill ' + (status === 'ANOMALY' ? 'bar-anomaly' : '');

    // Marker ngưỡng
    document.getElementById('thrMarker').style.left  = (thr * 100) + '%';
    document.getElementById('thrDisplay').innerText  = thr.toFixed(2);

    // [SỬA] Đồng bộ slider nếu threshold thay đổi từ server
    // TRƯỚC: slider.value  ← lỗi! biến 'slider' không tồn tại (id là 'thrSlider')
    // SAU:   document.getElementById('thrSlider').value
    if (!thrDragging && Math.abs(parseFloat(document.getElementById('thrSlider').value) - thr) > 0.001) {
        setSlider(thr);
    }
}

/**
 * THANH TRƯỢT THRESHOLD
 */
function setSlider(val) {
    document.getElementById('thrSlider').value    = val;
    document.getElementById('thrVal').innerText   = parseFloat(val).toFixed(2);
}

// Gọi khi bắt đầu kéo (mousedown/touchstart)
function onThrDragStart() {
    thrDragging = true;
}

// Gọi khi đang kéo: chỉ cập nhật hiển thị
function onThrInput(val) {
    thrDragging = true; // đảm bảo flag đang bật
    document.getElementById('thrVal').innerText = parseFloat(val).toFixed(2);
}

// Gọi khi nhả tay: gửi lệnh lưu threshold (không restart)
function onThrChange(val) {
    thrDragging = false;
    clearTimeout(thrSaveTimer);
    thrSaveTimer = setTimeout(() => {
        send('setThreshold', { threshold: parseFloat(val) });
        console.log('[AI] Threshold set to', val);
    }, 300);
}

/**
 * ĐIỀU KHIỂN KẾT NỐI SERVER MQTT
 */
function toggleServer() {
    const btn = document.getElementById('btnServer');
    if (!isSrvConnected) {
        send('setServer', { status: "CONNECT" });
        btn.innerText = "CONNECTING...";
        btn.style.background = "#ffc107";
        clearTimeout(connectTimeout);
        connectTimeout = setTimeout(() => {
            if (!isSrvConnected) {
                alert("Kết nối Server thất bại! Vui lòng kiểm tra lại Token/Server hoặc WiFi.");
                btn.innerText = "CONNECT SERVER";
                btn.style.background = "";
                send('setServer', { status: "DISCONNECT" });
            }
        }, 10000);
    } else {
        send('setServer', { status: "DISCONNECT" });
        btn.innerText = "DISCONNECTING...";
    }
}

/**
 * ẨN/HIỆN MẬT KHẨU
 */
function togglePass() {
    const p = document.getElementById('pass');
    const t = document.querySelector('.toggle-password');
    if (p.type === 'password') { p.type = 'text';     t.innerText = '🙈'; }
    else                       { p.type = 'password'; t.innerText = '👁️'; }
}

/**
 * GỬI DỮ LIỆU QUA WEBSOCKET
 */
function send(action, data) {
    if (ws && ws.readyState === 1)
        ws.send(JSON.stringify({ action: action, ...data }));
}

/**
 * ĐIỀU KHIỂN NEOPIXEL
 */
function setNeo(r, g, b, name) {
    send('setNeo', { r, g, b, name });
}

/**
 * LƯU CẤU HÌNH VÀ KHỞI ĐỘNG LẠI
 */
function save() {
    if (!confirm("Thiết bị sẽ khởi động lại để áp dụng cấu hình mới?")) return;
    send('saveConfig', {
        ssid:      document.getElementById('ssid').value,
        pass:      document.getElementById('pass').value,
        server:    document.getElementById('server').value,
        port:      document.getElementById('port').value,
        token:     document.getElementById('token').value,
        threshold: parseFloat(document.getElementById('thrSlider').value)
    });
    alert("Đã gửi lệnh lưu! Vui lòng chờ thiết bị khởi động lại...");
    setTimeout(() => window.location.reload(), 8000);
}

window.onload = init;