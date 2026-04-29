import { escapeHtml, formatTime } from "./shared.js";

export class RawMqttPanel {
  constructor(root, { store, toast }) {
    this.root = root;
    this.store = store;
    this.toast = toast;
    this.renderShell();
    this.store.subscribe("rawMessages", (messages) => this.renderMessages(messages));
    this.renderMessages(this.store.getState().rawMessages);
  }

  renderShell() {
    this.root.innerHTML = `
      <div class="panel-inline-toolbar">
        <span class="badge">只读</span>
        <button class="secondary" type="button" data-role="clear">清空</button>
      </div>
      <div class="raw-mqtt-list" data-role="list"></div>
    `;
    this.list = this.root.querySelector('[data-role="list"]');
    this.root.querySelector('[data-role="clear"]').addEventListener("click", () => {
      this.store.clearRawMessages();
      this.toast("Raw MQTT 已清空");
    });
  }

  renderMessages(messages) {
    if (!messages.length) {
      this.list.innerHTML = '<div class="empty-state"><div><strong>暂无 MQTT 消息</strong><span>连接后，这里会显示最近收到的订阅消息。</span></div></div>';
      return;
    }
    this.list.innerHTML = "";
    for (const message of messages) {
      const row = document.createElement("div");
      row.className = "raw-mqtt-row";
      row.innerHTML = `
        <span>${escapeHtml(formatTime(message.timestamp))}</span>
        <span>${escapeHtml(message.topic)}</span>
        <span>${escapeHtml(this.previewPayload(message.payload))}</span>
      `;
      this.list.appendChild(row);
    }
  }

  previewPayload(payload) {
    const text = String(payload || "").replace(/\s+/g, " ").trim();
    return text.length > 220 ? `${text.slice(0, 220)}...` : text;
  }
}
