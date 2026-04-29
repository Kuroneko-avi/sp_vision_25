import { buildTopics, DEFAULT_ROBOT_ID, parseTelemetryPayload, sanitizeRobotId, validateParamsCurrent, validateParamsSchema } from "./core/protocol.js";
import { DashboardStore } from "./core/store.js";
import { LayoutManager } from "./core/layout_manager.js";
import { MqttTransport } from "./core/mqtt_transport.js";
import { createDefaultPanelRegistry } from "./core/panel_registry.js";
import { UiState } from "./ui_state.js";
import { toTimestampMs } from "./panels/shared.js";

const ui = new UiState();
ui.hydrateSavedSettings();

const store = new DashboardStore();
let activeRobotId = sanitizeRobotId(ui.robotIdInput.value || DEFAULT_ROBOT_ID);
let topics = buildTopics(activeRobotId);
store.setConnection({ robotId: activeRobotId, topics, status: "offline" });

const layout = new LayoutManager(
  document.getElementById("dashboard-grid"),
  createDefaultPanelRegistry(),
  {
    store,
    publishJson,
    toast: (message, type) => ui.showToast(message, type)
  }
);
layout.mount();

const transport = new MqttTransport({
  onStatus: (status) => {
    store.setStatus(status);
    ui.setStatus(status);
    syncControls();
  },
  onError: (error, phase) => {
    const prefix = phase === "subscribe" ? "subscribe failed" : "mqtt";
    appendSystemLog("error", `${prefix}: ${error.message}`);
    ui.showToast(`${prefix}: ${error.message}`, "error");
  },
  onSubscribed: () => appendSystemLog("info", "subscriptions ready"),
  onPublishBlocked: (topic) => {
    appendSystemLog("warn", `not connected: ${topic}`);
    ui.showToast("当前未连接 MQTT Broker", "warn");
  },
  onPublishError: (topic, error) => {
    appendSystemLog("error", `publish ${topic}: ${error.message}`);
    ui.showToast(`发布失败：${error.message}`, "error");
  },
  onPublishSuccess: (topic, payload) => {
    appendSystemLog("debug", `published ${topic} ${payload.request_id}`);
    ui.showToast(`已发布：${payload.command || payload.key}`);
  },
  onInvalidJson: (topic, error) => appendSystemLog("error", `${topic}: invalid JSON: ${error.message}`),
  onRawMessage: (entry) => store.appendRawMessage(entry),
  onMessage: ({ kind, message }) => dispatchMessage(kind, message)
});

ui.connectButton.addEventListener("click", connect);
ui.disconnectButton.addEventListener("click", disconnect);
ui.robotIdInput.addEventListener("input", previewTopics);
ui.brokerUrlInput.addEventListener("change", () => ui.saveSettings());
ui.robotIdInput.addEventListener("change", () => ui.saveSettings());
ui.brokerUrlInput.addEventListener("keydown", submitConnectionOnEnter);
ui.robotIdInput.addEventListener("keydown", submitConnectionOnEnter);

syncControls();

function publishJson(kind, payload, qos, onSuccess, onError) {
  const topicByKind = {
    param: topics.controlParam,
    command: topics.controlCmd
  };
  transport.publishJson(topicByKind[kind], payload, qos, onSuccess, onError);
}

function dispatchMessage(kind, message) {
  if (kind === "telemetry") {
    parseTelemetryPayload(message).values
      ? store.setTelemetry(message)
      : appendSystemLog("warn", "telemetry payload missing values/fields object");
  } else if (kind === "log") {
    store.appendLog({
      timestamp: toTimestampMs(message.timestamp),
      level: String(message.level || "info").toLowerCase(),
      message: String(message.message || message.msg || JSON.stringify(message))
    });
  } else if (kind === "paramsSchema") {
    store.setParamsSchema(validateParamsSchema(message));
  } else if (kind === "paramsCurrent") {
    const values = validateParamsCurrent(message);
    if (values) {
      store.setParamsCurrent(values);
    } else {
      appendSystemLog("warn", "params/current payload missing values object");
    }
  } else if (kind === "ack") {
    const result = layout.getPanel("ack").handleAck(message);
    layout.getPanel("params").handleAckResult(result.requestId, result.ok);
  }
}

function submitConnectionOnEnter(event) {
  if (event.key === "Enter" && !transport.active) {
    event.preventDefault();
    connect();
  }
}

function previewTopics() {
  if (transport.active) {
    return;
  }
  activeRobotId = sanitizeRobotId(ui.robotIdInput.value);
  topics = buildTopics(activeRobotId);
  store.setConnection({ robotId: activeRobotId, topics });
}

function connect() {
  const { url, robotId } = ui.normalizeConnectionInputs();
  activeRobotId = robotId;
  topics = buildTopics(activeRobotId);
  store.setConnection({ robotId: activeRobotId, topics, status: "connecting" });
  ui.saveSettings();
  syncControls();
  appendSystemLog("info", `connecting ${url}`);
  transport.connect(url, topics);
}

function disconnect() {
  if (!transport.active) {
    return;
  }
  transport.disconnect();
  store.setStatus("offline");
  syncControls();
  appendSystemLog("info", "disconnected");
  ui.showToast("已断开连接");
}

function syncControls() {
  const ready = transport.connected;
  ui.setControls(transport.active);
  layout.getPanel("commands")?.setConnectedState(ready);
  layout.getPanel("params")?.setConnectedState(ready);
}

function appendSystemLog(level, message) {
  store.appendLog({ timestamp: Date.now(), level, message });
}
