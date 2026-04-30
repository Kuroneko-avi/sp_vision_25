import { classifyTopic, getSubscriptions, QOS } from "./protocol.js";

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

    const activeClient = this.client;
    const isCurrentClient = () => this.client === activeClient;

    activeClient.on("connect", () => {
      if (isCurrentClient()) {
        this.subscribeAll(activeClient);
      }
    });
    activeClient.on("reconnect", () => {
      if (isCurrentClient()) {
        this.callbacks.onStatus?.("reconnecting");
      }
    });
    activeClient.on("offline", () => {
      if (isCurrentClient()) {
        this.callbacks.onStatus?.("offline");
      }
    });
    activeClient.on("close", () => {
      if (isCurrentClient()) {
        this.callbacks.onStatus?.("closed");
      }
    });
    activeClient.on("error", (error) => {
      if (isCurrentClient()) {
        this.callbacks.onStatus?.("error");
        this.callbacks.onError?.(error);
      }
    });
    activeClient.on("message", (topic, payload) => {
      if (isCurrentClient()) {
        this.dispatchMessage(topic, payload);
      }
    });
  }

  subscribeAll(activeClient = this.client) {
    activeClient.subscribe(getSubscriptions(this.topics), (error) => {
      if (this.client !== activeClient) {
        return;
      }
      if (error) {
        this.callbacks.onStatus?.("error");
        this.callbacks.onError?.(error, "subscribe");
        return;
      }
      this.callbacks.onStatus?.("online");
      this.callbacks.onSubscribed?.();
    });
  }

  disconnect(options = {}) {
    if (!this.client) {
      return;
    }
    const oldClient = this.client;
    this.client = null;
    oldClient.removeAllListeners();
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
    const rawPayload = payload.toString();
    this.callbacks.onRawMessage?.({ timestamp: Date.now(), topic, payload: rawPayload });

    let message;
    try {
      message = JSON.parse(rawPayload);
    } catch (error) {
      this.callbacks.onInvalidJson?.(topic, error);
      return;
    }

    this.callbacks.onMessage?.({
      kind: classifyTopic(topic, this.topics),
      topic,
      message
    });
  }
}
