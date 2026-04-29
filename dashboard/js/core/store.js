import { buildTopics, DEFAULT_ROBOT_ID } from "./protocol.js";

const MAX_RAW_MESSAGES = 200;
const MAX_LOGS = 500;
const MAX_ACKS = 20;

export class DashboardStore {
  constructor() {
    this.listeners = new Map();
    this.state = {
      robotId: DEFAULT_ROBOT_ID,
      topics: buildTopics(DEFAULT_ROBOT_ID),
      status: "offline",
      telemetry: null,
      paramsSchema: [],
      paramsCurrent: {},
      logs: [],
      acks: [],
      rawMessages: []
    };
  }

  getState() {
    return this.state;
  }

  subscribe(event, listener) {
    if (!this.listeners.has(event)) {
      this.listeners.set(event, new Set());
    }
    this.listeners.get(event).add(listener);
    return () => this.listeners.get(event)?.delete(listener);
  }

  emit(event, payload) {
    this.listeners.get(event)?.forEach((listener) => listener(payload, this.state));
    this.listeners.get("*")?.forEach((listener) => listener(event, payload, this.state));
  }

  setConnection({ robotId, topics, status }) {
    if (robotId) {
      this.state.robotId = robotId;
    }
    if (topics) {
      this.state.topics = topics;
    }
    if (status) {
      this.state.status = status;
    }
    this.emit("connection", { robotId: this.state.robotId, topics: this.state.topics, status: this.state.status });
  }

  setStatus(status) {
    this.state.status = status;
    this.emit("status", status);
  }

  setTelemetry(telemetry) {
    this.state.telemetry = telemetry;
    this.emit("telemetry", telemetry);
  }

  setParamsSchema(paramsSchema) {
    this.state.paramsSchema = paramsSchema;
    this.emit("paramsSchema", paramsSchema);
  }

  setParamsCurrent(values) {
    this.state.paramsCurrent = { ...this.state.paramsCurrent, ...values };
    this.emit("paramsCurrent", this.state.paramsCurrent);
  }

  appendLog(entry) {
    this.state.logs = [...this.state.logs, entry].slice(-MAX_LOGS);
    this.emit("logs", this.state.logs);
  }

  clearLogs() {
    this.state.logs = [];
    this.emit("logs", this.state.logs);
  }

  appendAck(entry) {
    this.state.acks = [entry, ...this.state.acks].slice(0, MAX_ACKS);
    this.emit("acks", this.state.acks);
  }

  appendRawMessage(entry) {
    this.state.rawMessages = [entry, ...this.state.rawMessages].slice(0, MAX_RAW_MESSAGES);
    this.emit("rawMessages", this.state.rawMessages);
  }

  clearRawMessages() {
    this.state.rawMessages = [];
    this.emit("rawMessages", this.state.rawMessages);
  }
}
