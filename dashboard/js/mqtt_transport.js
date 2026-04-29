import { getSubscriptions, QOS } from "./protocol.js";

export const MQTT_PORT = 9001;
const RECONNECT_MS = 2000;

export function defaultBrokerUrl() {
  const host = window.location.hostname || "localhost";
  return `ws://${host}:${MQTT_PORT}`;
}

export class MqttTransport {
  constructor(callbacks = {}) {
    this.callbacks = callbacks;
    this.client = null;
    this.topics = null;
  }

  get connected() {
    return Boolean(this.client && this.client.connected);
  }

  get active() {
    return Boolean(this.client);
  }

  connect(url, topics) {
    this.disconnect({ silent: true });
    this.topics = topics;
    this.callbacks.onStatus?.("connecting");

    try {
      this.client = mqtt.connect(url, {
        clean: true,
        clientId: `spvision_dashboard_${Math.random().toString(16).slice(2)}`,
        connectTimeout: 4000,
        reconnectPeriod: RECONNECT_MS
      });
    } catch (error) {
      this.client = null;
      this.callbacks.onStatus?.("offline");
      this.callbacks.onError?.(error);
      return;
    }

    this.client.on("connect", () => this.subscribeAll());
    this.client.on("reconnect", () => this.callbacks.onStatus?.("reconnecting"));
    this.client.on("offline", () => this.callbacks.onStatus?.("offline"));
    this.client.on("close", () => {
      if (this.client) {
        this.callbacks.onStatus?.("closed");
      }
    });
    this.client.on("error", (error) => {
      this.callbacks.onStatus?.("error");
      this.callbacks.onError?.(error);
    });
    this.client.on("message", (topic, payload) => this.dispatchMessage(topic, payload));
  }

  subscribeAll() {
    this.callbacks.onStatus?.("online");
    this.client.subscribe(getSubscriptions(this.topics), (error) => {
      if (error) {
        this.callbacks.onError?.(error, "subscribe");
        return;
      }
      this.callbacks.onSubscribed?.();
    });
  }

  disconnect(options = {}) {
    if (!this.client) {
      return;
    }
    const oldClient = this.client;
    this.client = null;
    oldClient.end(true);
    if (!options.silent) {
      this.callbacks.onStatus?.("offline");
    }
  }

  publishJson(topic, payload, qos = QOS.CONTROL_PUBLISH, onSuccess, onError) {
    if (!this.connected) {
      const error = new Error("not connected");
      this.callbacks.onPublishBlocked?.(topic, error);
      onError?.(error);
      return;
    }
    this.client.publish(topic, JSON.stringify(payload), { qos }, (error) => {
      if (error) {
        this.callbacks.onPublishError?.(topic, error);
        onError?.(error);
        return;
      }
      this.callbacks.onPublishSuccess?.(topic, payload);
      onSuccess?.();
    });
  }

  dispatchMessage(topic, payload) {
    let message;
    try {
      message = JSON.parse(payload.toString());
    } catch (error) {
      this.callbacks.onInvalidJson?.(topic, error);
      return;
    }

    if (topic === this.topics.data) {
      this.callbacks.onTelemetry?.(message);
    } else if (topic === this.topics.log) {
      this.callbacks.onLog?.(message);
    } else if (topic === this.topics.paramsSchema) {
      this.callbacks.onParamsSchema?.(message);
    } else if (topic === this.topics.paramsCurrent) {
      this.callbacks.onParamsCurrent?.(message);
    } else if (topic === this.topics.controlAck) {
      this.callbacks.onAck?.(message);
    }
  }
}
