import { buildTopics, DEFAULT_ROBOT_ID, sanitizeRobotId } from "./protocol.js";
import { MqttTransport } from "./mqtt_transport.js";
import { CommandPanel } from "./command_panel.js";
import { LogPanel } from "./log_panel.js";
import { ParamsPanel } from "./params_panel.js";
import { TelemetryPanel } from "./telemetry_panel.js";
import { UiState } from "./ui_state.js";

const ui = new UiState();
ui.hydrateSavedSettings();

let activeRobotId = sanitizeRobotId(ui.robotIdInput.value || DEFAULT_ROBOT_ID);
let topics = buildTopics(activeRobotId);
ui.updateTopicLabels(topics);

const logPanel = new LogPanel({
  onToast: (message, type) => ui.showToast(message, type)
});

const telemetryPanel = new TelemetryPanel({
  onWarn: (message) => logPanel.appendSystemLog("warn", message),
  onToast: (message, type) => ui.showToast(message, type)
});

const transport = new MqttTransport({
  onStatus: (status) => {
    ui.setStatus(status);
    syncControls();
  },
  onError: (error, phase) => {
    const prefix = phase === "subscribe" ? "subscribe failed" : "mqtt";
    logPanel.appendSystemLog("error", `${prefix}: ${error.message}`);
    ui.showToast(`${prefix}: ${error.message}`, "error");
  },
  onSubscribed: () => logPanel.appendSystemLog("info", "subscriptions ready"),
  onPublishBlocked: (topic) => {
    logPanel.appendSystemLog("warn", `not connected: ${topic}`);
    ui.showToast("当前未连接 MQTT Broker", "warn");
  },
  onPublishError: (topic, error) => {
    logPanel.appendSystemLog("error", `publish ${topic}: ${error.message}`);
    ui.showToast(`发布失败：${error.message}`, "error");
  },
  onPublishSuccess: (topic, payload) => {
    logPanel.appendSystemLog("debug", `published ${topic} ${payload.request_id}`);
    ui.showToast(`已发布：${payload.command || payload.key}`);
  },
  onInvalidJson: (topic, error) => logPanel.appendSystemLog("error", `${topic}: invalid JSON: ${error.message}`),
  onTelemetry: (message) => telemetryPanel.handleTelemetry(message),
  onLog: (message) => logPanel.handleLog(message),
  onParamsSchema: (message) => paramsPanel.handleParamSchema(message),
  onParamsCurrent: (message) => paramsPanel.handleParamCurrent(message),
  onAck: (message) => {
    const result = commandPanel.handleAck(message);
    paramsPanel.handleAckResult(result.requestId, result.ok);
  }
});

const publishJson = (kind, payload, qos, onSuccess, onError) => {
  const topicByKind = {
    param: topics.controlParam,
    command: topics.controlCmd
  };
  transport.publishJson(topicByKind[kind], payload, qos, onSuccess, onError);
};

const paramsPanel = new ParamsPanel({
  publishJson,
  onLog: (level, message) => logPanel.appendSystemLog(level, message),
  onToast: (message, type) => ui.showToast(message, type)
});

const commandPanel = new CommandPanel({
  publishJson,
  onToast: (message, type) => ui.showToast(message, type)
});

ui.connectButton.addEventListener("click", connect);
ui.disconnectButton.addEventListener("click", disconnect);
ui.robotIdInput.addEventListener("input", () => {
  if (!transport.active) {
    ui.previewTopics();
  }
});
ui.brokerUrlInput.addEventListener("change", () => ui.saveSettings());
ui.robotIdInput.addEventListener("change", () => ui.saveSettings());
ui.brokerUrlInput.addEventListener("keydown", submitConnectionOnEnter);
ui.robotIdInput.addEventListener("keydown", submitConnectionOnEnter);

syncControls();

function submitConnectionOnEnter(event) {
  if (event.key === "Enter" && !transport.active) {
    event.preventDefault();
    connect();
  }
}

function connect() {
  const { url, robotId } = ui.normalizeConnectionInputs();
  activeRobotId = robotId;
  topics = buildTopics(activeRobotId);
  ui.updateTopicLabels(topics);
  ui.saveSettings();
  syncControls();
  logPanel.appendSystemLog("info", `connecting ${url}`);
  transport.connect(url, topics);
}

function disconnect() {
  if (!transport.active) {
    return;
  }
  transport.disconnect();
  syncControls();
  logPanel.appendSystemLog("info", "disconnected");
  ui.showToast("已断开连接");
}

function syncControls() {
  ui.setControls(transport.active, transport.connected, paramsPanel);
}
