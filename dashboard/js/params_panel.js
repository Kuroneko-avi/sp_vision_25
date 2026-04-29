import { buildControlParamPayload, QOS, validateParamsCurrent, validateParamsSchema } from "./protocol.js";
import { escapeHtml, formatRawValue } from "./ui_state.js";

export class ParamsPanel {
  constructor({ publishJson, onLog, onToast }) {
    this.publishJson = publishJson;
    this.onLog = onLog;
    this.onToast = onToast;
    this.paramsMeta = document.getElementById("params-meta");
    this.paramsList = document.getElementById("params-list");
    this.paramFilterInput = document.getElementById("param-filter");
    this.resetParamFilterButton = document.getElementById("reset-param-filter");
    this.dirtyParamCountLabel = document.getElementById("dirty-param-count");
    this.applyDirtyParamsButton = document.getElementById("apply-dirty-params");
    this.resetDirtyParamsButton = document.getElementById("reset-dirty-params");
    this.paramEntries = new Map();
    this.paramControls = new Map();
    this.currentParamValues = new Map();
    this.dirtyParams = new Set();
    this.draftParamValues = new Map();
    this.pendingParamRequests = new Map();
    this.ready = false;
    this.hasClient = false;

    this.paramFilterInput.addEventListener("input", () => this.renderParams());
    this.resetParamFilterButton.addEventListener("click", () => {
      this.paramFilterInput.value = "";
      this.renderParams();
    });
    this.applyDirtyParamsButton.addEventListener("click", () => this.applyDirtyParams());
    this.resetDirtyParamsButton.addEventListener("click", () => this.resetDirtyParams());
    window.addEventListener("beforeunload", (event) => {
      if (!this.dirtyParams.size) {
        return;
      }
      event.preventDefault();
      event.returnValue = "还有未应用的参数修改。";
    });
  }

  setConnectedState(ready, hasClient) {
    this.ready = ready;
    this.hasClient = hasClient;
    this.paramControls.forEach(({ entry, apply }) => {
      apply.disabled = !ready || !this.isParamEditable(entry);
    });
    this.updateDirtySummary();
  }

  handleParamSchema(message) {
    const params = validateParamsSchema(message);
    this.paramEntries.clear();
    for (const param of params) {
      const entry = { ...param };
      if (this.currentParamValues.has(param.key)) {
        entry.value = this.currentParamValues.get(param.key);
      }
      this.paramEntries.set(param.key, entry);
    }
    this.updateParamsMeta();
    this.renderParams();
  }

  handleParamCurrent(message) {
    const values = validateParamsCurrent(message);
    if (!values) {
      this.onLog?.("warn", "params/current payload missing values object");
      return;
    }

    for (const [key, value] of Object.entries(values)) {
      this.currentParamValues.set(key, value);
      const entry = this.paramEntries.get(key);
      if (entry) {
        entry.value = value;
      }
      this.syncParamControl(key, value);
    }
    this.updateParamsMeta();
  }

  updateParamsMeta(visibleCount = null) {
    const total = this.paramEntries.size;
    if (visibleCount !== null && visibleCount !== total) {
      this.paramsMeta.textContent = `${visibleCount}/${total} 个参数`;
    } else {
      this.paramsMeta.textContent = total ? `${total} 个参数` : "等待 schema";
    }
  }

  renderParams() {
    this.paramControls.clear();
    this.paramsList.innerHTML = "";

    if (!this.paramEntries.size) {
      this.paramsList.innerHTML = '<div class="empty-state"><div><strong>还没有参数 schema</strong><span>连接设备后，点击右侧“刷新参数”，等待 params/schema 与 params/current 到达。</span></div></div>';
      this.updateParamsMeta();
      this.setConnectedState(this.ready, this.hasClient);
      return;
    }

    const keyword = this.paramFilterInput.value.trim().toLowerCase();
    const groups = new Map();
    let visibleCount = 0;
    for (const entry of this.paramEntries.values()) {
      const group = String(entry.group || "default");
      const searchable = `${entry.key} ${entry.type} ${group} ${entry.unit || ""}`.toLowerCase();
      if (keyword && !searchable.includes(keyword)) {
        continue;
      }
      if (!groups.has(group)) {
        groups.set(group, []);
      }
      groups.get(group).push(entry);
      visibleCount += 1;
    }

    if (!visibleCount) {
      this.paramsList.innerHTML = '<div class="empty-state"><div><strong>没有匹配的参数</strong><span>换一个关键词，或点击“重置搜索”查看全部参数。</span></div></div>';
      this.updateParamsMeta(0);
      this.setConnectedState(this.ready, this.hasClient);
      return;
    }

    for (const [group, entries] of groups) {
      const groupNode = document.createElement("div");
      groupNode.className = "param-group";
      const title = document.createElement("div");
      title.className = "param-group-title";
      title.innerHTML = `<span>${escapeHtml(group)}</span><span>${entries.length}</span>`;
      groupNode.appendChild(title);
      for (const entry of entries) {
        groupNode.appendChild(this.createParamRow(entry));
      }
      this.paramsList.appendChild(groupNode);
    }
    this.updateParamsMeta(visibleCount);
    this.setConnectedState(this.ready, this.hasClient);
  }

  createParamRow(entry) {
    const editable = this.isParamEditable(entry);
    const row = document.createElement("div");
    row.className = "param-row";
    row.dataset.key = entry.key;

    const status = document.createElement("div");
    status.className = "param-status";
    const name = document.createElement("div");
    name.className = "param-name";
    name.textContent = entry.key;
    const currentValue = document.createElement("div");
    currentValue.className = "current-value";
    currentValue.textContent = this.formatParamValue(entry.value, entry.unit);
    const detail = document.createElement("div");
    detail.className = "param-detail";
    detail.textContent = this.getParamDetail(entry);
    const paramState = document.createElement("div");
    paramState.className = "param-state synced";
    paramState.textContent = "已同步";
    status.append(name, currentValue, detail, paramState);

    const inputs = document.createElement("div");
    inputs.className = "param-inputs";
    const apply = document.createElement("button");
    apply.className = "primary";
    apply.type = "button";
    apply.textContent = editable ? "应用" : "只读";
    apply.disabled = !editable;

    const inputEntry = this.dirtyParams.has(entry.key) ? { ...entry, value: this.draftParamValues.get(entry.key) } : entry;
    const controls = this.buildParamInputs(inputEntry);
    inputs.classList.toggle("single", controls.length === 1);
    controls.forEach((control) => inputs.appendChild(control));
    apply.addEventListener("click", () => this.publishParam(entry.key));

    if (editable) {
      for (const control of controls) {
        const input = control.matches?.("input, select, textarea")
          ? control
          : control.querySelector?.("input, select, textarea");
        if (input) {
          input.addEventListener("input", () => this.markParamDirty(entry.key, true));
          input.addEventListener("change", () => this.markParamDirty(entry.key, true));
          input.addEventListener("keydown", (event) => {
            if (event.key === "Enter") {
              event.preventDefault();
              this.publishParam(entry.key);
            }
          });
        }
      }
    } else {
      for (const control of controls) {
        const input = control.matches?.("input, select, textarea")
          ? control
          : control.querySelector?.("input, select, textarea");
        if (input) {
          input.disabled = true;
          input.readOnly = true;
        }
      }
    }

    row.append(status, inputs, apply);
    this.paramControls.set(entry.key, { entry, row, controls, currentValue, paramState, apply });
    if (!editable) {
      this.setParamState(entry.key, "readonly", entry.restart_required ? "重启生效" : "只读");
    } else if (this.dirtyParams.has(entry.key)) {
      this.setParamState(entry.key, "dirty", "待应用");
    } else if ([...this.pendingParamRequests.values()].includes(entry.key)) {
      this.setParamState(entry.key, "pending", "等待回执");
    }
    return row;
  }

  getParamDetail(entry) {
    const parts = [`类型：${entry.type}`];
    parts.push(this.isParamEditable(entry) ? "热更新" : "只读");
    if (entry.restart_required) {
      parts.push("重启后生效");
    }
    if (entry.render === "json") {
      parts.push("JSON");
    }
    if (entry.source_key) {
      parts.push(`源：${entry.source_key}`);
    }
    if (entry.unit) {
      parts.push(`单位：${entry.unit}`);
    }
    if (Number.isFinite(Number(entry.min)) || Number.isFinite(Number(entry.max))) {
      const min = Number.isFinite(Number(entry.min)) ? entry.min : "-∞";
      const max = Number.isFinite(Number(entry.max)) ? entry.max : "+∞";
      parts.push(`范围：${min} ~ ${max}`);
    }
    if (Number.isFinite(Number(entry.step))) {
      parts.push(`步长：${entry.step}`);
    }
    return parts.join(" · ");
  }

  isParamEditable(entry) {
    return entry && entry.editable !== false && entry.read_only !== true;
  }

  markParamDirty(key, dirty) {
    const control = this.paramControls.get(key);
    if (!control || !this.isParamEditable(control.entry)) {
      return;
    }
    const value = this.readParamValue(control);
    const hasChanged = dirty && !this.valuesEqual(value, control.entry.value);
    if (hasChanged) {
      this.dirtyParams.add(key);
      this.draftParamValues.set(key, value);
      this.setParamState(key, "dirty", "待应用");
    } else {
      this.dirtyParams.delete(key);
      this.draftParamValues.delete(key);
      this.setParamState(key, "synced", "已同步");
    }
    this.updateDirtySummary();
    this.setConnectedState(this.ready, this.hasClient);
  }

  setParamState(key, state, text) {
    const control = this.paramControls.get(key);
    if (!control) {
      return;
    }
    control.row.classList.remove("dirty", "pending", "success", "fail");
    control.paramState.classList.remove("dirty", "pending", "success", "fail", "synced", "readonly");
    if (state !== "synced" && state !== "readonly") {
      control.row.classList.add(state === "dirty" ? "dirty" : state);
    }
    control.paramState.classList.add(state);
    control.paramState.textContent = text;
  }

  updateDirtySummary() {
    this.dirtyParamCountLabel.innerHTML = `待应用：<strong>${this.dirtyParams.size}</strong>`;
    this.applyDirtyParamsButton.disabled = !this.ready || this.dirtyParams.size === 0;
    this.resetDirtyParamsButton.disabled = this.dirtyParams.size === 0;
  }

  valuesEqual(a, b) {
    const na = Number(a);
    const nb = Number(b);
    if (Number.isFinite(na) && Number.isFinite(nb)) {
      return na === nb;
    }
    return formatRawValue(a) === formatRawValue(b);
  }

  buildParamInputs(entry) {
    const editable = this.isParamEditable(entry);
    if (entry.render === "json") {
      const textarea = document.createElement("textarea");
      textarea.className = "param-json";
      textarea.value = formatRawValue(entry.value);
      textarea.disabled = !editable;
      textarea.readOnly = !editable;
      return [textarea];
    }

    if (entry.type === "number") {
      const range = document.createElement("input");
      range.type = "range";
      this.applyNumberBounds(range, entry);
      range.value = this.numberOrDefault(entry.value, range.min || 0);
      range.disabled = !editable;
      const number = document.createElement("input");
      number.type = "number";
      this.applyNumberBounds(number, entry);
      number.value = range.value;
      number.disabled = !editable;
      number.readOnly = !editable;
      range.addEventListener("input", () => {
        number.value = range.value;
      });
      number.addEventListener("input", () => {
        if (number.value !== "") {
          range.value = number.value;
        }
      });
      return [range, number];
    }

    if (entry.type === "bool") {
      const wrap = document.createElement("label");
      wrap.className = "bool-input";
      const checkbox = document.createElement("input");
      checkbox.type = "checkbox";
      checkbox.checked = Boolean(entry.value);
      checkbox.disabled = !editable;
      const label = document.createElement("span");
      label.textContent = checkbox.checked ? "开启" : "关闭";
      checkbox.addEventListener("change", () => {
        label.textContent = checkbox.checked ? "开启" : "关闭";
      });
      wrap.append(checkbox, label);
      return [wrap];
    }

    if (entry.type === "enum") {
      const select = document.createElement("select");
      select.disabled = !editable;
      const options = Array.isArray(entry.options) ? entry.options : [];
      if (!options.length) {
        const empty = document.createElement("option");
        empty.value = "";
        empty.textContent = "无可选项";
        select.appendChild(empty);
      }
      for (const option of options) {
        const node = document.createElement("option");
        node.value = String(option);
        node.textContent = String(option);
        select.appendChild(node);
      }
      select.value = String(entry.value ?? "");
      return [select];
    }

    const input = document.createElement("input");
    input.type = "text";
    input.value = formatRawValue(entry.value);
    input.disabled = !editable;
    input.readOnly = !editable;
    return [input];
  }

  applyNumberBounds(input, entry) {
    if (Number.isFinite(Number(entry.min))) {
      input.min = String(entry.min);
    }
    if (Number.isFinite(Number(entry.max))) {
      input.max = String(entry.max);
    }
    input.step = Number.isFinite(Number(entry.step)) ? String(entry.step) : "any";
  }

  syncParamControl(key, value) {
    const control = this.paramControls.get(key);
    if (!control) {
      return;
    }
    control.entry.value = value;
    control.currentValue.textContent = this.formatParamValue(value, control.entry.unit);
    if (!this.dirtyParams.has(key)) {
      this.writeParamControlValue(control, value);
      if (this.isParamEditable(control.entry)) {
        this.setParamState(key, "synced", "已同步");
      } else {
        this.setParamState(key, "readonly", control.entry.restart_required ? "重启生效" : "只读");
      }
    }
  }

  writeParamControlValue(control, value) {
    if (control.entry.type === "number") {
      for (const item of control.controls) {
        item.value = String(value);
      }
    } else if (control.entry.type === "bool") {
      const checkbox = control.controls[0].querySelector("input");
      const label = control.controls[0].querySelector("span");
      checkbox.checked = Boolean(value);
      if (label) {
        label.textContent = checkbox.checked ? "开启" : "关闭";
      }
    } else {
      control.controls[0].value = formatRawValue(value);
    }
  }

  publishParam(key) {
    const control = this.paramControls.get(key);
    if (!control) {
      return;
    }
    if (!this.isParamEditable(control.entry)) {
      this.onLog?.("warn", `${key}: read-only parameter`);
      this.onToast?.(`${key} 当前为只读参数`, "warn");
      return;
    }
    const value = this.readParamValue(control);
    if (control.entry.type === "number" && !Number.isFinite(value)) {
      this.onLog?.("warn", `${key}: invalid number`);
      this.onToast?.(`${key} 不是有效数字`, "warn");
      return;
    }
    const payload = buildControlParamPayload(key, value);
    this.publishJson("param", payload, QOS.CONTROL_PUBLISH, () => {
      this.pendingParamRequests.set(payload.request_id, key);
      this.dirtyParams.delete(key);
      this.draftParamValues.delete(key);
      this.setParamState(key, "pending", "等待回执");
      this.updateDirtySummary();
    }, () => {
      this.setParamState(key, "fail", "发布失败");
    });
  }

  applyDirtyParams() {
    if (!this.dirtyParams.size) {
      this.onToast?.("没有待应用的参数", "warn");
      return;
    }
    for (const key of Array.from(this.dirtyParams)) {
      if (this.paramControls.has(key)) {
        this.publishParam(key);
      }
    }
  }

  resetDirtyParams() {
    if (!this.dirtyParams.size) {
      return;
    }
    for (const key of Array.from(this.dirtyParams)) {
      const control = this.paramControls.get(key);
      if (!control) {
        this.dirtyParams.delete(key);
        this.draftParamValues.delete(key);
        continue;
      }
      this.writeParamControlValue(control, control.entry.value);
      this.dirtyParams.delete(key);
      this.draftParamValues.delete(key);
      this.setParamState(key, "synced", "已同步");
    }
    this.updateDirtySummary();
    this.onToast?.("已撤销未应用修改");
  }

  readParamValue(control) {
    if (control.entry.type === "number") {
      return Number(control.controls[1].value);
    }
    if (control.entry.type === "bool") {
      return control.controls[0].querySelector("input").checked;
    }
    return control.controls[0].value;
  }

  handleAckResult(requestId, ok) {
    if (!this.pendingParamRequests.has(requestId)) {
      return;
    }
    const key = this.pendingParamRequests.get(requestId);
    this.pendingParamRequests.delete(requestId);
    this.setParamState(key, ok ? "success" : "fail", ok ? "已确认" : "执行失败");
    if (ok) {
      window.setTimeout(() => {
        if (!this.dirtyParams.has(key) && ![...this.pendingParamRequests.values()].includes(key)) {
          this.setParamState(key, "synced", "已同步");
        }
      }, 1800);
    }
  }

  numberOrDefault(value, fallback) {
    const number = Number(value);
    return Number.isFinite(number) ? String(number) : String(fallback);
  }

  formatParamValue(value, unit) {
    const suffix = unit ? ` ${unit}` : "";
    if (value === undefined || value === null || value === "") {
      return `当前值：-${suffix}`;
    }
    return `当前值：${formatRawValue(value)}${suffix}`;
  }
}
