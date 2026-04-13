let ws;
let appState = {
  receiver: null,
  senders: [],
  unknownDevices: []
};
let selectedDeviceId = null;
let receiverDraftDirty = false;
let senderDraftDirty = false;

function init() {
  bindDraftInputs();
  connectSocket();
  syncInteractionGuards();
}

function connectSocket() {
  ws = new WebSocket(`ws://${window.location.host}/ws`);

  ws.onopen = () => {
    setWsState(true, "Đã kết nối receiver");
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
      if (data.success) {
        closeNewDevice();
      }
      alert(data.message || (data.success ? "Thành công" : "Thất bại"));
    }
  };
}

function bindDraftInputs() {
  ["receiverSsid", "receiverPass", "receiverServer", "receiverPort", "receiverToken"].forEach((id) => {
    const el = document.getElementById(id);
    if (el) {
      el.addEventListener("input", () => {
        receiverDraftDirty = true;
      });
    }
  });

  ["senderName", "senderThreshold"].forEach((id) => {
    const el = document.getElementById(id);
    if (el) {
      el.addEventListener("input", () => {
        senderDraftDirty = true;
      });
    }
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
    alert("Receiver chưa sẵn sàng.");
    return;
  }

  ws.send(JSON.stringify({ action, ...payload }));
}

function isModalOpen(id) {
  return !document.getElementById(id).classList.contains("hidden");
}

function syncValueIfIdle(id, value, dirtyFlag) {
  const element = document.getElementById(id);
  if (!element) return;
  if (dirtyFlag) return;
  if (document.activeElement === element) return;
  element.value = value ?? "";
}

function setText(id, value) {
  const element = document.getElementById(id);
  if (element) {
    element.innerText = value;
  }
}

function setStatusTone(id, connected) {
  const element = document.getElementById(id);
  if (!element) return;
  element.classList.toggle("status-good", connected);
  element.classList.toggle("status-bad", !connected);
}

function statusLabel(connected) {
  return connected ? "Online" : "Offline";
}

function isSetupGatewayHost() {
  return window.location.hostname === "192.168.7.1";
}

function canManageReceiver() {
  return isSetupGatewayHost() && ws && ws.readyState === 1;
}

function syncInteractionGuards() {
  const blockedByHost = !isSetupGatewayHost();
  const blockedBySocket = !ws || ws.readyState !== 1;
  const note = document.getElementById("uxGuardNote");

  let message = "";
  if (blockedByHost) {
    message = "Đang ở chế độ chỉ xem. Hãy truy cập đúng địa chỉ 192.168.7.1 để thêm thiết bị hoặc đổi cấu hình receiver.";
  } else if (blockedBySocket) {
    message = "Receiver chưa kết nối xong. Các thao tác cấu hình sẽ tự mở lại khi WebSocket sẵn sàng.";
  }

  if (note) {
    note.innerText = message;
    note.classList.toggle("hidden", !message);
  }

  document.querySelectorAll("[data-requires-setup='true']").forEach((element) => {
    element.disabled = !canManageReceiver();
  });

  [
    "newDeviceMac",
    "newDeviceName",
    "senderName",
    "senderThreshold",
    "receiverSsid",
    "receiverPass",
    "receiverServer",
    "receiverPort",
    "receiverToken"
  ].forEach((id) => {
    const input = document.getElementById(id);
    if (input) {
      input.disabled = !canManageReceiver();
    }
  });
}

function findSelectedSender() {
  return (appState.senders || []).find((item) => item.deviceId === selectedDeviceId) || null;
}

function render() {
  renderSummary();
  renderKnownSenders();
  renderUnknownDevices();
  syncReceiverModal();
  syncSenderDrawer();
  syncInteractionGuards();
}

function renderSummary() {
  const senders = appState.senders || [];
  const unknownDevices = appState.unknownDevices || [];
  const online = senders.filter((item) => item.online).length;
  const receiver = appState.receiver || {};
  const cloudConnected = receiver.cloudStatus === "Đã kết nối";

  setText("statTotal", senders.length);
  setText("statOnline", online);
  setText("statUnknown", unknownDevices.length);
  setText("statCloud", receiver.cloudStatus || "--");
  setStatusTone("statCloud", cloudConnected);
  setText(
    "cloudSummary",
    `Cloud: ${receiver.cloudStatus || "--"} | Kênh: ${receiver.channel || "--"} | TCP: ${receiver.localTcpPort || "--"}`
  );
  setStatusTone("cloudSummary", cloudConnected);
}

function renderKnownSenders() {
  const grid = document.getElementById("senderGrid");
  const empty = document.getElementById("knownEmptyState");
  const senders = [...(appState.senders || [])];

  if (!senders.length) {
    grid.innerHTML = "";
    empty.classList.remove("hidden");
    return;
  }

  empty.classList.add("hidden");

  senders.sort((a, b) => {
    if (a.online !== b.online) return a.online ? -1 : 1;
    return (a.name || "").localeCompare(b.name || "");
  });

  grid.innerHTML = senders.map((sender) => {
    const onlineClass = sender.online ? "online" : "offline";
    const aiClass = sender.aiStatus === "ANOMALY" ? "badge-alert" : "badge-ok";
    const connectivityClass = sender.online ? "badge-online" : "badge-offline";
    return `
      <button class="sender-card ${onlineClass}" onclick="openSender('${escapeHtml(sender.deviceId)}')">
        <div class="sender-card-head">
          <div>
            <span class="sender-name">${escapeHtml(sender.name || "Undefined")}</span>
            <span class="sender-mac">${escapeHtml(sender.deviceId)}</span>
          </div>
          <span class="connection-pill ${connectivityClass}">
            <span class="dot ${onlineClass}"></span>
            ${statusLabel(sender.online)}
          </span>
        </div>
        <div class="sender-metrics">
          <div>
            <span>Nhiệt độ</span>
            <strong>${formatNumber(sender.temperature, "°C")}</strong>
          </div>
          <div>
            <span>Độ ẩm</span>
            <strong>${formatNumber(sender.humidity, "%")}</strong>
          </div>
        </div>
        <div class="sender-footer">
          <span class="badge ${aiClass}">${escapeHtml(sender.aiStatus || "NO DATA")}</span>
          <span>${escapeHtml(sender.ip || "--")}</span>
        </div>
      </button>
    `;
  }).join("");
}

function renderUnknownDevices() {
  const grid = document.getElementById("unknownGrid");
  const empty = document.getElementById("unknownEmptyState");
  const unknownDevices = [...(appState.unknownDevices || [])];

  if (!unknownDevices.length) {
    grid.innerHTML = "";
    empty.classList.remove("hidden");
    return;
  }

  empty.classList.add("hidden");

  unknownDevices.sort((a, b) => (a.deviceId || "").localeCompare(b.deviceId || ""));

  grid.innerHTML = unknownDevices.map((device) => {
    const onlineClass = device.online ? "online" : "offline";
    const connectivityClass = device.online ? "badge-online" : "badge-offline";
    const disabled = canManageReceiver() ? "" : "disabled";
    return `
      <article class="sender-card unknown-card ${onlineClass}">
        <div class="sender-card-head">
          <div>
            <span class="sender-name">Undefined</span>
            <span class="sender-mac">${escapeHtml(device.deviceId)}</span>
          </div>
          <span class="connection-pill ${connectivityClass}">
            <span class="dot ${onlineClass}"></span>
            ${statusLabel(device.online)}
          </span>
        </div>
        <div class="unknown-meta">
          <span>IP: ${escapeHtml(device.ip || "--")}</span>
          <span>Số lần thấy: ${device.seenCount || 0}</span>
          <span>${formatAge(device.lastSeenAgeMs)}</span>
        </div>
        <button class="btn btn-secondary btn-full" ${disabled} onclick="openNewDevice('${escapeHtml(device.deviceId)}')">Thêm thiết bị này</button>
      </article>
    `;
  }).join("");
}

function syncReceiverModal() {
  const receiver = appState.receiver || {};

  syncValueIfIdle("receiverSsid", receiver.wifiSsid || "", receiverDraftDirty);
  syncValueIfIdle("receiverPass", receiver.wifiPass || "", receiverDraftDirty);
  syncValueIfIdle("receiverServer", receiver.server || "", receiverDraftDirty);
  syncValueIfIdle("receiverPort", receiver.port || 1883, receiverDraftDirty);
  syncValueIfIdle("receiverToken", receiver.token || "", receiverDraftDirty);

  setText("receiverMac", receiver.receiverMac || "--");
  setText("receiverChannel", receiver.channel || "--");
  setText("receiverStaIp", receiver.wifiIp || "--");
  setText("receiverApIp", receiver.apIp || "--");
}

function syncSenderDrawer() {
  if (!selectedDeviceId) {
    return;
  }

  const sender = findSelectedSender();
  if (!sender) {
    closeSenderDrawer();
    return;
  }

  const receiver = appState.receiver || {};
  setText("drawerTitle", sender.name || "Undefined");
  setText("detailDeviceId", sender.deviceId || "--");
  setText("detailSenderIp", sender.ip || "--");
  setText("detailOnline", sender.online ? "Online" : "Offline");
  setText("detailSeen", formatAge(sender.lastSeenAgeMs));
  setText("detailSync", sender.syncPending ? "Đang chờ đồng bộ" : "Đã đồng bộ");
  setText("detailTemp", formatNumber(sender.temperature, "°C"));
  setText("detailHumi", formatNumber(sender.humidity, "%"));
  setText("detailStatus", sender.aiStatus || "--");
  setText("detailScore", formatNumber(sender.aiScore, ""));
  setText("detailLat", Number.isFinite(sender.latitude) ? Number(sender.latitude).toFixed(6) : "--");
  setText("detailLng", Number.isFinite(sender.longitude) ? Number(sender.longitude).toFixed(6) : "--");
  setText("detailTs", formatTimestamp(sender.senderTimestampMs));
  setText("detailReceiverMac", receiver.receiverMac || "--");
  setText("detailTcpPort", receiver.localTcpPort || "--");

  setStatusTone("detailOnline", !!sender.online);
  setStatusTone("detailSync", !sender.syncPending);
  setStatusTone("detailStatus", sender.aiStatus !== "ANOMALY");

  if (!senderDraftDirty) {
    syncValueIfIdle("senderName", sender.name || "", false);
    syncValueIfIdle(
      "senderThreshold",
      pickThreshold(sender) != null ? Number(pickThreshold(sender)).toFixed(2) : "",
      false
    );
  }
}

function pickThreshold(sender) {
  if (Number.isFinite(sender.desiredThreshold)) return sender.desiredThreshold;
  if (Number.isFinite(sender.reportedThreshold)) return sender.reportedThreshold;
  return null;
}

function formatNumber(value, suffix) {
  if (!Number.isFinite(value)) return "--";
  const fixed = Math.abs(value) >= 100 ? value.toFixed(1) : value.toFixed(2);
  return `${fixed}${suffix}`;
}

function formatAge(ms) {
  if (!ms) return "--";
  if (ms < 1000) return "Vừa xong";
  const seconds = Math.floor(ms / 1000);
  if (seconds < 60) return `${seconds}s trước`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}p trước`;
  const hours = Math.floor(minutes / 60);
  return `${hours}h trước`;
}

function formatTimestamp(timestampMs) {
  if (!Number.isFinite(timestampMs) || timestampMs <= 0) return "--";

  return new Intl.DateTimeFormat("vi-VN", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false
  }).format(new Date(timestampMs));
}

function escapeHtml(text) {
  return String(text)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function openSender(deviceId) {
  selectedDeviceId = deviceId;
  senderDraftDirty = false;
  document.getElementById("senderDrawer").classList.remove("hidden");
  document.getElementById("overlay").classList.remove("hidden");
  syncSenderDrawer();
  syncInteractionGuards();
}

function closeSenderDrawer() {
  selectedDeviceId = null;
  senderDraftDirty = false;
  document.getElementById("senderDrawer").classList.add("hidden");
  hideOverlayIfUnused();
}

function openNewDevice(prefillDeviceId = "") {
  if (!canManageReceiver()) {
    syncInteractionGuards();
    return;
  }

  document.getElementById("newDeviceMac").value = prefillDeviceId;
  document.getElementById("newDeviceName").value = "";
  document.getElementById("newDeviceModal").classList.remove("hidden");
  document.getElementById("overlay").classList.remove("hidden");
}

function closeNewDevice() {
  document.getElementById("newDeviceModal").classList.add("hidden");
  hideOverlayIfUnused();
}

function openReceiverSettings() {
  if (!canManageReceiver()) {
    syncInteractionGuards();
    return;
  }

  receiverDraftDirty = false;
  document.getElementById("receiverModal").classList.remove("hidden");
  document.getElementById("overlay").classList.remove("hidden");
  syncReceiverModal();
}

function closeReceiverSettings() {
  receiverDraftDirty = false;
  document.getElementById("receiverModal").classList.add("hidden");
  hideOverlayIfUnused();
}

function closeAllOverlays() {
  closeSenderDrawer();
  closeNewDevice();
  closeReceiverSettings();
}

function hideOverlayIfUnused() {
  const senderOpen = isModalOpen("senderDrawer");
  const newDeviceOpen = isModalOpen("newDeviceModal");
  const receiverOpen = isModalOpen("receiverModal");
  if (!senderOpen && !newDeviceOpen && !receiverOpen) {
    document.getElementById("overlay").classList.add("hidden");
  }
}

function toggleReceiverPassword() {
  const input = document.getElementById("receiverPass");
  const button = document.getElementById("toggleReceiverPass");
  const showing = input.type === "text";
  input.type = showing ? "password" : "text";
  button.innerText = showing ? "Hiện" : "Ẩn";
}

function saveNewDevice() {
  if (!canManageReceiver()) return;

  send("addDevice", {
    deviceId: document.getElementById("newDeviceMac").value.trim(),
    name: document.getElementById("newDeviceName").value.trim()
  });
}

function saveSelectedSender() {
  if (!canManageReceiver()) return;
  if (!selectedDeviceId) return;

  senderDraftDirty = false;
  send("saveSenderConfig", {
    deviceId: selectedDeviceId,
    name: document.getElementById("senderName").value.trim(),
    threshold: parseFloat(document.getElementById("senderThreshold").value)
  });
}

function deleteSelectedSender() {
  if (!canManageReceiver()) return;

  const sender = findSelectedSender();
  if (!sender) return;
  if (!confirm(`Xóa sender "${sender.name}" khỏi danh sách hiện tại?`)) {
    return;
  }

  send("deleteSender", { deviceId: selectedDeviceId });
  closeSenderDrawer();
}

function saveReceiverSettings() {
  if (!canManageReceiver()) return;
  if (!confirm("Receiver sẽ khởi động lại để áp dụng cấu hình mới. Tiếp tục?")) {
    return;
  }

  receiverDraftDirty = false;
  send("saveReceiverConfig", {
    ssid: document.getElementById("receiverSsid").value.trim(),
    pass: document.getElementById("receiverPass").value,
    server: document.getElementById("receiverServer").value.trim(),
    port: parseInt(document.getElementById("receiverPort").value, 10) || 1883,
    token: document.getElementById("receiverToken").value.trim()
  });
}

window.onload = init;
