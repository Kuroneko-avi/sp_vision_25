import { buildControlCmdPayload, QOS } from "./protocol.js";
import { escapeHtml, formatDateTime, toTimestampMs } from "./ui_state.js";

const MAX_ACKS = 20;

export class CommandPanel {
  constructor({ publishJson, onToast }) {
    this.publishJson = publishJson;
    this.onToast = onToast;
    this.ackList = document.getElementById("ack-list");
    this.ackEntries = [];

    document.querySelectorAll(".command-button").forEach((button) => {
      button.addEventListener("click", () => this.publishCommand(button.dataset.command));
    });
  }

  publishCommand(command) {
    if (command === "stop_dashboard") {
      const ok = window.confirm("确认发送 stop_dashboard 吗？该操作可能会停止数据发布。 ");
      if (!ok) {
        return;
      }
    }
    this.publishJson("command", buildControlCmdPayload(command), QOS.CONTROL_PUBLISH);
  }

  handleAck(message) {
    const timestamp = toTimestampMs(message.timestamp);
    const requestId = String(message.request_id || "-");
    const ok = Boolean(message.ok);
    const ackMessage = String(message.message || message.reason || "-");
    this.ackEntries.unshift({ requestId, ok, message: ackMessage, timestamp });
    while (this.ackEntries.length > MAX_ACKS) {
      this.ackEntries.pop();
    }
    if (!ok) {
      this.onToast?.(`回执失败：${ackMessage}`, "error");
    }
    this.renderAcks();
    return { requestId, ok };
  }

  renderAcks() {
    if (!this.ackEntries.length) {
      this.ackList.innerHTML = '<div class="empty-state"><div><strong>暂无控制回执</strong><span>发送命令或应用参数后，这里会显示成功/失败结果。</span></div></div>';
      return;
    }
    this.ackList.innerHTML = "";
    for (const ack of this.ackEntries) {
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
