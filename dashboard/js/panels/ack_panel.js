import { escapeHtml, formatDateTime } from "./shared.js";

export class AckPanel {
  constructor(root, { store, toast }) {
    this.root = root;
    this.store = store;
    this.toast = toast;
    this.renderShell();
    this.store.on("connection", ({ topics }) => this.updateTopic(topics));
    this.store.on("acks", (acks) => this.renderAcks(acks));
    this.updateTopic(this.store.getState().topics);
    this.renderAcks(this.store.getState().acks);
  }

  renderShell() {
    this.root.innerHTML = `
      <div class="panel-inline-toolbar"><span class="topic-pill" data-role="topic">-</span></div>
      <div class="ack-list" data-role="list"></div>
    `;
    this.topicLabel = this.root.querySelector('[data-role="topic"]');
    this.ackList = this.root.querySelector('[data-role="list"]');
  }

  updateTopic(topics) {
    this.topicLabel.textContent = topics.controlAck;
  }

  handleAck(message) {
    const requestId = String(message.request_id || "-");
    const ok = Boolean(message.ok);
    const ackMessage = String(message.message || message.reason || "-");
    const timestamp = Number.isFinite(Number(message.timestamp)) ? Number(message.timestamp) : Date.now();
    this.store.appendAck({ requestId, ok, message: ackMessage, timestamp });
    if (!ok) {
      this.toast(`回执失败：${ackMessage}`, "error");
    }
    return { requestId, ok };
  }

  renderAcks(acks) {
    if (!acks.length) {
      this.ackList.innerHTML = '<div class="empty-state"><div><strong>暂无控制回执</strong><span>发送命令或应用参数后，这里会显示成功/失败结果。</span></div></div>';
      return;
    }
    this.ackList.innerHTML = "";
    for (const ack of acks) {
      const node = document.createElement("div");
      node.className = `ack-item ${ack.ok ? "" : "fail"}`;
      node.innerHTML = `
        <div class="ack-topline">
          <div class="ack-id">${escapeHtml(ack.requestId)}</div>
          <span class="badge ${ack.ok ? "good" : "fail"}">${ack.ok ? "成功" : "失败"}</span>
        </div>
        <div class="ack-message">${escapeHtml(ack.message)}</div>
        <div class="ack-time">${escapeHtml(formatDateTime(ack.timestamp))}</div>
      `;
      this.ackList.appendChild(node);
    }
  }
}
