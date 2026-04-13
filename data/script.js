let ws;
let appState = {};
let settingsDirty = false;

// ============================================================
//  Khởi động ứng dụng và WebSocket
// ============================================================

function init() {
  bindDraftInputs();
  bindThresholdSlider();
  connectSocket();
  syncInteractionGuards();
}

function connectSocket() {
  ws = new WebSocket(`ws://${window.location.host}/ws`);

  ws.onopen = () => {
    setWsState(true, "Đã kết nối sender");
    syncInteractionGuards();
  };

  ws.onclose = () => {
    setWsState(false, "Đang thử kết nối lại...");
    syncInteractionGuards();
    setTimeout(connectSocket, 2000);
  };

  ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    if (data.type === "snapshot") {
      appState = data;
      render();
      return;
    }

    if (data.type === "actionResult") {
      alert(data.message || (data.success ? "Thành công" : "Thất bại"));
    }
  };
}

// ============================================================
//  Tiện ích UI chung
// ============================================================

function bindDraftInputs() {
  ["wifiSsid", "wifiPass", "receiverIp", "receiverPort"].forEach((id) => {
    const el = document.getElementById(id);
    if (!el) return;
    el.addEventListener("input", () => {
      settingsDirty = true;
    });
  });
}

function bindThresholdSlider() {
  const slider = document.getElementById("thresholdSlider");
  if (!slider) return;
  slider.addEventListener("input", () => {
    document.getElementById("thresholdValue").innerText = Number(slider.value).toFixed(2);
  });
}

function setWsState(connected, text) {
  const dot = document.getElementById("wsDot");
  const label = document.getElementById("wsLabel");
  if (dot) {
    dot.style.background = connected ? "#0f9d58" : "#d93025";
  }
  if (label) {
    label.innerText = text;
  }
}

function send(action, payload = {}) {
  if (!ws || ws.readyState !== 1) {
    alert("Sender chưa sẵn sàng.");
    return;
  }

  ws.send(JSON.stringify({ action, ...payload }));
}

function setText(id, value) {
  const el = document.getElementById(id);
  if (el) {
    el.innerText = value;
  }
}

function setStatusTone(id, connected) {
  const el = document.getElementById(id);
  if (!el) return;
  el.classList.toggle("status-good", connected);
  el.classList.toggle("status-bad", !connected);
}

function isSetupGatewayHost() {
  return window.location.hostname === "192.168.7.1";
}

function canManageSender() {
  return isSetupGatewayHost() && ws && ws.readyState === 1;
}

function syncInteractionGuards() {
  const blockedByHost = !isSetupGatewayHost();
  const blockedBySocket = !ws || ws.readyState !== 1;
  const note = document.getElementById("uxGuardNote");

  let message = "";
  if (blockedByHost) {
    message = "Đang ở chế độ chỉ xem. Hãy truy cập đúng địa chỉ 192.168.7.1 để cấu hình sender.";
  } else if (blockedBySocket) {
    message = "Sender chưa kết nối xong. Các thao tác cấu hình sẽ mở lại khi WebSocket sẵn sàng.";
  }

  if (note) {
    note.innerText = message;
    note.classList.toggle("hidden", !message);
  }

  document.querySelectorAll("[data-requires-setup='true']").forEach((el) => {
    el.disabled = !canManageSender();
  });

  ["wifiSsid", "wifiPass", "receiverIp", "receiverPort", "thresholdSlider"].forEach((id) => {
    const input = document.getElementById(id);
    if (input) {
      input.disabled = !canManageSender();
    }
  });
}

function syncValueIfIdle(id, value) {
  const el = document.getElementById(id);
  if (!el) return;
  if (settingsDirty && ["wifiSsid", "wifiPass", "receiverIp", "receiverPort"].includes(id)) return;
  if (document.activeElement === el) return;
  el.value = value ?? "";
}

function formatNumber(value, suffix = "") {
  if (value === null || value === undefined || Number.isNaN(Number(value))) {
    return "--";
  }
  return `${Number(value).toFixed(2)}${suffix}`;
}

// ============================================================
//  Render giao diện từ snapshot
// ============================================================

function render() {
  const receiverConnected = !!appState.receiverConnected;
  const anomaly = appState.aiStatus === "ANOMALY";
  const targetReceiverIp = appState.resolvedReceiverIp || appState.receiverIp || "--";
  const autoDetected = !appState.receiverIp && !!appState.resolvedReceiverIp;

  setText("statTemp", formatNumber(appState.temperature, "°C"));
  setText("statHumi", formatNumber(appState.humidity, "%"));
  setText("statAi", appState.aiStatus || "--");
  setText("statReceiver", appState.receiverStatus || "--");
  setStatusTone("statAi", !anomaly);
  setStatusTone("statReceiver", receiverConnected);

  setText("metricTemp", formatNumber(appState.temperature, "°C"));
  setText("metricHumi", formatNumber(appState.humidity, "%"));
  setText("metricScore", formatNumber(appState.aiScore, ""));
  setText("metricStatus", appState.aiStatus || "--");
  setStatusTone("metricStatus", !anomaly);

  setText("deviceId", appState.deviceId || "--");
  setText("deviceName", appState.deviceName || "Undefined");
  setText("apSsid", appState.apSsid || "--");
  setText("apIp", appState.apIp || "--");
  setText("staIp", appState.staIp || "--");
  setText("resolvedReceiverIpText", targetReceiverIp);
  setText("configVersion", String(appState.configVersion || 0));
  setText(
    "receiverSummary",
    `Receiver: ${appState.receiverStatus || "--"} | ${targetReceiverIp}:${appState.receiverPort || "--"}`
  );

  const receiverIpHint = document.getElementById("receiverIpHint");
  if (receiverIpHint) {
    receiverIpHint.innerText = autoDetected
      ? `IP đang dùng: ${targetReceiverIp} (tự suy ra theo Wi-Fi hiện tại)`
      : `IP đang dùng: ${targetReceiverIp}`;
  }

  syncValueIfIdle("wifiSsid", appState.wifiSsid || "");
  syncValueIfIdle("wifiPass", appState.wifiPass || "");
  syncValueIfIdle("receiverIp", appState.receiverIp || "");
  syncValueIfIdle("receiverPort", appState.receiverPort || 4100);

  const slider = document.getElementById("thresholdSlider");
  if (slider && document.activeElement !== slider) {
    slider.value = Number(appState.threshold || 0.5).toFixed(2);
    document.getElementById("thresholdValue").innerText = Number(slider.value).toFixed(2);
  }

  setText("receiverStatusText", appState.receiverStatus || "--");
  setText("wifiStatusText", appState.staConnected ? "Đã kết nối" : "Chưa kết nối");
  setStatusTone("receiverStatusText", receiverConnected);
  setStatusTone("wifiStatusText", !!appState.staConnected);

  syncInteractionGuards();
}

// ============================================================
//  Tương tác người dùng
// ============================================================

function openSettings() {
  document.getElementById("settingsModal").classList.remove("hidden");
  document.getElementById("overlay").classList.remove("hidden");
}

function closeSettings() {
  document.getElementById("settingsModal").classList.add("hidden");
  document.getElementById("overlay").classList.add("hidden");
  settingsDirty = false;
}

function saveConfig() {
  send("saveConfig", {
    wifiSsid: document.getElementById("wifiSsid").value.trim(),
    wifiPass: document.getElementById("wifiPass").value,
    receiverIp: document.getElementById("receiverIp").value.trim(),
    receiverPort: Number(document.getElementById("receiverPort").value || 4100),
    threshold: Number(document.getElementById("thresholdSlider").value)
  });
}

function saveThreshold() {
  send("setThreshold", {
    threshold: Number(document.getElementById("thresholdSlider").value)
  });
}

function togglePassword() {
  const input = document.getElementById("wifiPass");
  const button = document.getElementById("toggleWifiPass");
  if (!input || !button) return;
  const visible = input.type === "text";
  input.type = visible ? "password" : "text";
  button.innerText = visible ? "Hiện" : "Ẩn";
}

window.addEventListener("load", init);
