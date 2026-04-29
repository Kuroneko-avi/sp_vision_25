import { escapeHtml, formatDateTime, formatTime } from "./shared.js";

export class LogViewerPanel {
  constructor(root, { store, toast }) {
    this.root = root;
    this.store = store;
    this.toast = toast;
    this.logScrollPaused = false;
    this.renderShell();
    this.store.subscribe("logs", (logs) => this.renderLogs(logs));
    this.renderLogs(this.store.getState().logs);
  }

  renderShell() {
    this.root.innerHTML = `
      <div class="log-toolbar">
        <select data-role="level" aria-label="日志级别筛选">
          <option value="all">全部日志</option>
          <option value="debug">Debug</option>
          <option value="info">Info</option>
          <option value="warn">Warn+</option>
          <option value="error">Error+</option>
        </select>
        <button class="secondary" type="button" data-role="toggle">暂停滚动</button>
        <button class="secondary" type="button" data-role="copy">复制日志</button>
        <button class="secondary" type="button" data-role="clear">清空日志</button>
      </div>
      <div class="log-body" data-role="list"></div>
    `;
    this.logList = this.root.querySelector('[data-role="list"]');
    this.logLevelFilter = this.root.querySelector('[data-role="level"]');
    this.toggleButton = this.root.querySelector('[data-role="toggle"]');
    this.logLevelFilter.addEventListener("change", () => this.renderLogs(this.store.getState().logs));
    this.toggleButton.addEventListener("click", () => this.toggleScroll());
    this.root.querySelector('[data-role="copy"]').addEventListener("click", () => this.copyVisibleLogs());
    this.root.querySelector('[data-role="clear"]').addEventListener("click", () => this.clearLogs());
  }

  renderLogs(logs) {
    const visible = this.getVisibleLogs(logs);
    if (!visible.length) {
      this.logList.innerHTML = logs.length
        ? '<div class="empty-state"><div><strong>当前筛选下没有日志</strong><span>可以切换日志级别筛选，或等待新的日志到达。</span></div></div>'
        : '<div class="empty-state"><div><strong>暂无运行日志</strong><span>连接、订阅、发布、解析异常和机器人日志都会显示在这里。</span></div></div>';
      return;
    }

    this.logList.innerHTML = "";
    for (const entry of visible) {
      const row = document.createElement("div");
      const level = this.normalizeLogLevel(entry.level);
      row.className = `log-entry ${level}`;
      row.innerHTML = `
        <span>${escapeHtml(formatTime(entry.timestamp))}</span>
        <span class="log-level">${escapeHtml(level)}</span>
        <span class="log-message">${escapeHtml(entry.message)}</span>
      `;
      this.logList.appendChild(row);
    }

    if (!this.logScrollPaused) {
      this.scrollLogsToBottom();
    }
  }

  getVisibleLogs(logs = this.store.getState().logs) {
    const filter = this.logLevelFilter.value;
    if (filter === "all") {
      return logs;
    }
    const priority = { debug: 0, info: 1, warn: 2, error: 3, critical: 3 };
    const minPriority = priority[filter] ?? 0;
    return logs.filter((entry) => (priority[this.normalizeLogLevel(entry.level)] ?? 1) >= minPriority);
  }

  async copyVisibleLogs() {
    const text = this.getVisibleLogs()
      .map((entry) => `[${formatDateTime(entry.timestamp)}] ${this.normalizeLogLevel(entry.level).toUpperCase()} ${entry.message}`)
      .join("\n");
    if (!text) {
      this.toast("当前没有可复制的日志", "warn");
      return;
    }
    try {
      await navigator.clipboard.writeText(text);
      this.toast("日志已复制");
    } catch (_) {
      const textarea = document.createElement("textarea");
      textarea.value = text;
      document.body.appendChild(textarea);
      textarea.select();
      document.execCommand("copy");
      textarea.remove();
      this.toast("日志已复制");
    }
  }

  normalizeLogLevel(level) {
    if (["debug", "info", "warn", "error", "critical"].includes(level)) {
      return level;
    }
    if (level === "warning") {
      return "warn";
    }
    return "info";
  }

  toggleScroll() {
    this.logScrollPaused = !this.logScrollPaused;
    this.toggleButton.textContent = this.logScrollPaused ? "恢复滚动" : "暂停滚动";
    if (!this.logScrollPaused) {
      this.scrollLogsToBottom();
    }
  }

  clearLogs() {
    if (this.store.getState().logs.length && !window.confirm("确认清空当前日志显示吗？这不会影响机器人端日志。")) {
      return;
    }
    this.store.clearLogs();
    this.toast("日志已清空");
  }

  scrollLogsToBottom() {
    this.logList.scrollTop = this.logList.scrollHeight;
  }
}
