import { escapeHtml, formatDateTime, formatTime, toTimestampMs } from "./ui_state.js";

const MAX_LOGS = 500;

export class LogPanel {
  constructor({ onToast }) {
    this.onToast = onToast;
    this.logList = document.getElementById("log-list");
    this.toggleLogScrollButton = document.getElementById("toggle-log-scroll");
    this.clearLogButton = document.getElementById("clear-log");
    this.copyLogButton = document.getElementById("copy-log");
    this.logLevelFilter = document.getElementById("log-level-filter");
    this.logScrollPaused = false;
    this.logEntries = [];

    this.logLevelFilter.addEventListener("change", () => this.renderLogs());
    this.copyLogButton.addEventListener("click", () => this.copyVisibleLogs());
    this.toggleLogScrollButton.addEventListener("click", () => this.toggleScroll());
    this.clearLogButton.addEventListener("click", () => this.clearLogs());
  }

  handleLog(message) {
    const timestamp = toTimestampMs(message.timestamp);
    const level = String(message.level || "info").toLowerCase();
    const text = String(message.message || message.msg || JSON.stringify(message));
    this.appendLog({ timestamp, level, message: text });
  }

  appendSystemLog(level, message) {
    this.appendLog({ timestamp: Date.now(), level, message });
  }

  appendLog(entry) {
    this.logEntries.push(entry);
    while (this.logEntries.length > MAX_LOGS) {
      this.logEntries.shift();
    }
    this.renderLogs();
  }

  renderLogs() {
    const visible = this.getVisibleLogs();
    if (!visible.length) {
      this.logList.innerHTML = this.logEntries.length
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

  getVisibleLogs() {
    const filter = this.logLevelFilter.value;
    if (filter === "all") {
      return this.logEntries;
    }
    const priority = { debug: 0, info: 1, warn: 2, error: 3, critical: 3 };
    const minPriority = priority[filter] ?? 0;
    return this.logEntries.filter((entry) => (priority[this.normalizeLogLevel(entry.level)] ?? 1) >= minPriority);
  }

  async copyVisibleLogs() {
    const text = this.getVisibleLogs()
      .map((entry) => `[${formatDateTime(entry.timestamp)}] ${this.normalizeLogLevel(entry.level).toUpperCase()} ${entry.message}`)
      .join("\n");
    if (!text) {
      this.onToast?.("当前没有可复制的日志", "warn");
      return;
    }
    try {
      await navigator.clipboard.writeText(text);
      this.onToast?.("日志已复制");
    } catch (_) {
      const textarea = document.createElement("textarea");
      textarea.value = text;
      document.body.appendChild(textarea);
      textarea.select();
      document.execCommand("copy");
      textarea.remove();
      this.onToast?.("日志已复制");
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
    this.toggleLogScrollButton.textContent = this.logScrollPaused ? "恢复滚动" : "暂停滚动";
    if (!this.logScrollPaused) {
      this.scrollLogsToBottom();
    }
  }

  clearLogs() {
    if (this.logEntries.length && !window.confirm("确认清空当前日志显示吗？这不会影响机器人端日志。")) {
      return;
    }
    this.logEntries.length = 0;
    this.renderLogs();
    this.onToast?.("日志已清空");
  }

  scrollLogsToBottom() {
    this.logList.scrollTop = this.logList.scrollHeight;
  }
}
