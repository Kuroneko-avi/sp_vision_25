import { DEFAULT_ROBOT_ID, buildTopics, sanitizeRobotId } from "./protocol.js";
import { defaultBrokerUrl } from "./mqtt_transport.js";

const STORAGE_KEY = "spvision-dashboard-settings";

export class UiState {
  constructor() {
    this.brokerUrlInput = document.getElementById("broker-url");
    this.robotIdInput = document.getElementById("robot-id");
    this.connectButton = document.getElementById("connect");
    this.disconnectButton = document.getElementById("disconnect");
    this.statusDot = document.getElementById("status-dot");
    this.statusLabel = document.getElementById("status-label");
    this.telemetryTopicLabel = document.getElementById("telemetry-topic");
    this.commandTopicLabel = document.getElementById("command-topic");
    this.ackTopicLabel = document.getElementById("ack-topic");
    this.toastWrap = document.getElementById("toast-wrap");
    this.commandButtons = Array.from(document.querySelectorAll(".command-button"));
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

  updateTopicLabels(topics) {
    this.telemetryTopicLabel.textContent = topics.data;
    this.commandTopicLabel.textContent = topics.controlCmd;
    this.ackTopicLabel.textContent = topics.controlAck;
  }

  previewTopics() {
    this.updateTopicLabels(buildTopics(this.currentRobotId()));
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

  setControls(hasClient, ready, paramsPanel) {
    this.connectButton.disabled = hasClient;
    this.disconnectButton.disabled = !hasClient;
    this.brokerUrlInput.disabled = hasClient;
    this.robotIdInput.disabled = hasClient;
    this.commandButtons.forEach((button) => {
      button.disabled = !ready;
    });
    paramsPanel?.setConnectedState(ready, hasClient);
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

export function toTimestampMs(value) {
  const timestamp = Number(value);
  if (!Number.isFinite(timestamp)) {
    return Date.now();
  }
  return timestamp > 100000000000 ? timestamp : timestamp * 1000;
}

export function formatTime(timestamp) {
  return new Date(timestamp).toLocaleTimeString();
}

export function formatDateTime(timestamp) {
  return new Date(timestamp).toLocaleString();
}

export function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

export function formatRawValue(value) {
  if (value === undefined || value === null) {
    return "";
  }
  if (typeof value === "object") {
    try {
      return JSON.stringify(value);
    } catch (error) {
      return String(value);
    }
  }
  return String(value);
}
