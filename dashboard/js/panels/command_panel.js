import { buildControlCmdPayload, QOS } from "../core/protocol.js";

export class CommandPanel {
  constructor(root, { publishJson, store }) {
    this.root = root;
    this.publishJson = publishJson;
    this.store = store;
    this.renderShell();
    this.store.subscribe("connection", ({ topics }) => this.updateTopic(topics));
    this.updateTopic(this.store.getState().topics);
  }

  renderShell() {
    this.root.innerHTML = `
      <div class="panel-inline-toolbar"><span class="topic-pill" data-role="topic">-</span></div>
      <div class="command-body">
        <div class="command-grid">
          <div class="command-card">
            <div><strong>启动 Dashboard</strong><span>发布 start_dashboard，恢复数据与控制服务。</span></div>
            <button class="primary command-button" type="button" data-command="start_dashboard">启动</button>
          </div>
          <div class="command-card">
            <div><strong>停止 Dashboard</strong><span>发布 stop_dashboard。该操作会先弹窗确认，避免误触。</span></div>
            <button class="danger command-button" type="button" data-command="stop_dashboard">停止</button>
          </div>
          <div class="command-card">
            <div><strong>重新发布参数</strong><span>让下位/服务端重新广播 schema 与当前值。</span></div>
            <button class="secondary command-button" type="button" data-command="republish_params">刷新参数</button>
          </div>
        </div>
      </div>
    `;
    this.topicLabel = this.root.querySelector('[data-role="topic"]');
    this.buttons = Array.from(this.root.querySelectorAll(".command-button"));
    this.buttons.forEach((button) => {
      button.addEventListener("click", () => this.publishCommand(button.dataset.command));
    });
  }

  updateTopic(topics) {
    this.topicLabel.textContent = topics.controlCmd;
  }

  setConnectedState(ready) {
    this.buttons.forEach((button) => {
      button.disabled = !ready;
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
}
