import { DEFAULT_ROBOT_ID, sanitizeRobotId } from "./core/protocol.js";
import { defaultBrokerUrl } from "./core/mqtt_transport.js";

const STORAGE_KEY = "spvision-dashboard-settings";

export class UiState {
  constructor() {
    this.brokerUrlInput = document.getElementById("broker-url");
    this.robotIdInput = document.getElementById("robot-id");
    this.connectButton = document.getElementById("connect");
    this.disconnectButton = document.getElementById("disconnect");
    this.statusDot = document.getElementById("status-dot");
    this.statusLabel = document.getElementById("status-label");
    this.toastWrap = document.getElementById("toast-wrap");
  }

  hydrateSavedSettings() {
    let saved = null;
    try {
      saved = JSON.parse(localStorage.getItem(STORAGE_KEY) || "null");
    } catch (_) {
      saved = null;
    }
    this.brokerUrlInput.value = saved?.brokerUrl || defaultBrokerUrl();
    this.robotIdInput.value = saved?.robotId || DEFAULT_ROBOT_ID;
  }

  saveSettings() {
    const payload = {
      brokerUrl: this.brokerUrlInput.value.trim() || defaultBrokerUrl(),
      robotId: sanitizeRobotId(this.robotIdInput.value)
    };
    localStorage.setItem(STORAGE_KEY, JSON.stringify(payload));
  }

  currentRobotId() {
    return sanitizeRobotId(this.robotIdInput.value);
  }

  currentBrokerUrl() {
    return this.brokerUrlInput.value.trim() || defaultBrokerUrl();
  }

  normalizeConnectionInputs() {
    const url = this.currentBrokerUrl();
    const robotId = this.currentRobotId();
    this.brokerUrlInput.value = url;
    this.robotIdInput.value = robotId;
    return { url, robotId };
  }

  setStatus(statusKey) {
    const labels = {
      online: "已连接",
      connecting: "连接中",
      reconnecting: "重连中",
      offline: "未连接",
      closed: "已断开",
      error: "错误"
    };
    this.statusLabel.textContent = labels[statusKey] || labels.offline;
    const status = this.statusDot.parentElement;
    status.classList.remove("online", "connecting", "reconnecting", "error", "offline");
    status.classList.add(statusKey === "closed" ? "offline" : statusKey);
    this.statusDot.classList.toggle("online", statusKey === "online");
  }

  setControls(hasClient) {
    this.connectButton.disabled = hasClient;
    this.disconnectButton.disabled = !hasClient;
    this.brokerUrlInput.disabled = hasClient;
    this.robotIdInput.disabled = hasClient;
  }

  showToast(message, type = "info") {
    const toast = document.createElement("div");
    toast.className = `toast ${type}`;
    toast.textContent = message;
    this.toastWrap.appendChild(toast);
    window.setTimeout(() => {
      toast.style.opacity = "0";
      toast.style.transform = "translateY(8px)";
      window.setTimeout(() => toast.remove(), 220);
    }, 2600);
  }
}
