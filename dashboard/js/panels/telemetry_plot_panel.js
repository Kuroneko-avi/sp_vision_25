import { parseTelemetryPayload } from "../core/protocol.js";
import { formatTime, toTimestampMs } from "./shared.js";

const MAX_POINTS = 200;
const TELEMETRY_WINDOW_MS = 30000;

export class TelemetryPlotPanel {
  constructor(root, { store, toast }) {
    this.root = root;
    this.store = store;
    this.toast = toast;
    this.totalSamples = 0;
    this.lastTelemetryTimestamp = null;
    this.fieldSeries = new Map();
    this.hiddenTelemetrySeries = new Set();
    this.renderShell();
    this.chart = echarts.init(this.chartNode);
    this.initChart();
    this.updateTopic(this.store.getState().topics);
    this.updateChartMetrics();
    this.store.on("connection", ({ topics }) => this.updateTopic(topics));
    this.store.on("telemetry", (message) => this.handleTelemetry(message));
  }

  renderShell() {
    this.root.innerHTML = `
      <div class="panel-inline-toolbar">
        <span class="topic-pill" data-role="topic">-</span>
        <button class="secondary" type="button" data-role="clear">清空曲线</button>
      </div>
      <div class="chart-body"><div class="telemetry-chart" data-role="chart"></div></div>
      <div class="series-filter" data-role="series-filter" aria-label="曲线显示控制"></div>
      <div class="metrics">
        <div class="metric"><span>Samples</span><strong data-role="sample-count">0</strong></div>
        <div class="metric"><span>Series</span><strong data-role="series-count">0</strong></div>
        <div class="metric"><span>Last Data</span><strong data-role="last-data-time">-</strong></div>
        <div class="metric"><span>Source</span><strong data-role="telemetry-source">values</strong></div>
      </div>
    `;
    this.topicLabel = this.root.querySelector('[data-role="topic"]');
    this.chartNode = this.root.querySelector('[data-role="chart"]');
    this.seriesFilter = this.root.querySelector('[data-role="series-filter"]');
    this.sampleCountLabel = this.root.querySelector('[data-role="sample-count"]');
    this.seriesCountLabel = this.root.querySelector('[data-role="series-count"]');
    this.lastDataTimeLabel = this.root.querySelector('[data-role="last-data-time"]');
    this.telemetrySourceLabel = this.root.querySelector('[data-role="telemetry-source"]');
    this.root.querySelector('[data-role="clear"]').addEventListener("click", () => this.clearChart());
  }

  initChart() {
    this.chart.setOption({
      animation: false,
      color: ["#2563eb", "#16a34a", "#f97316", "#7c3aed", "#0891b2", "#dc2626", "#4b5563"],
      tooltip: {
        trigger: "axis",
        axisPointer: { type: "cross" },
        valueFormatter: (value) => (Number.isFinite(Number(value)) ? Number(value).toFixed(4) : value)
      },
      legend: { type: "scroll", top: 0, data: [] },
      grid: { top: 52, right: 24, bottom: 48, left: 60 },
      xAxis: {
        type: "time",
        boundaryGap: false,
        axisLabel: { color: "#667085" },
        axisLine: { lineStyle: { color: "#d0d7e2" } },
        splitLine: { show: false }
      },
      yAxis: {
        type: "value",
        scale: true,
        axisLabel: { color: "#667085" },
        splitLine: { lineStyle: { color: "#eef2f7" } }
      },
      dataZoom: [
        { type: "inside", xAxisIndex: 0, filterMode: "none" },
        { type: "slider", xAxisIndex: 0, height: 20, bottom: 10, filterMode: "none" }
      ],
      series: []
    });
  }

  updateTopic(topics) {
    this.topicLabel.textContent = topics.data;
  }

  handleTelemetry(message) {
    const telemetry = parseTelemetryPayload(message);
    if (!telemetry.values) {
      this.store.appendLog({ timestamp: Date.now(), level: "warn", message: "telemetry payload missing values/fields object" });
      return;
    }

    const timestamp = toTimestampMs(message.timestamp);
    this.lastTelemetryTimestamp = timestamp;
    const windowStart = timestamp - TELEMETRY_WINDOW_MS;
    let changed = false;
    for (const [key, value] of Object.entries(telemetry.values)) {
      const numericValue = Number(value);
      if (!Number.isFinite(numericValue)) {
        continue;
      }
      if (!this.fieldSeries.has(key)) {
        this.fieldSeries.set(key, []);
      }
      const points = this.fieldSeries.get(key);
      points.push([timestamp, numericValue]);
      this.pruneSeriesPoints(points, windowStart);
      changed = true;
    }

    if (changed) {
      this.totalSamples += 1;
      this.telemetrySourceLabel.textContent = telemetry.source;
      this.lastDataTimeLabel.textContent = formatTime(timestamp);
      this.renderChart();
    }
  }

  renderChart() {
    const keys = Array.from(this.fieldSeries.keys());
    const legendSelected = {};
    for (const key of keys) {
      legendSelected[key] = !this.hiddenTelemetrySeries.has(key);
    }
    const xAxis =
      this.lastTelemetryTimestamp === null
        ? {}
        : { min: this.lastTelemetryTimestamp - TELEMETRY_WINDOW_MS, max: this.lastTelemetryTimestamp };
    this.chart.setOption({
      xAxis,
      legend: { data: keys, selected: legendSelected },
      series: keys.map((key) => ({
        name: key,
        type: "line",
        showSymbol: false,
        smooth: false,
        show: !this.hiddenTelemetrySeries.has(key),
        data: this.fieldSeries.get(key),
        emphasis: { focus: "series" },
        lineStyle: { width: 2 }
      }))
    });
    this.renderSeriesFilter(keys);
    this.updateChartMetrics();
  }

  renderSeriesFilter(keys) {
    this.seriesFilter.innerHTML = "";
    for (const key of keys) {
      const label = document.createElement("label");
      label.className = "series-toggle";
      label.title = key;
      const checkbox = document.createElement("input");
      checkbox.type = "checkbox";
      checkbox.checked = !this.hiddenTelemetrySeries.has(key);
      checkbox.addEventListener("change", () => {
        if (checkbox.checked) {
          this.hiddenTelemetrySeries.delete(key);
        } else {
          this.hiddenTelemetrySeries.add(key);
        }
        this.renderChart();
      });
      const text = document.createElement("span");
      text.textContent = key;
      label.append(checkbox, text);
      this.seriesFilter.appendChild(label);
    }
  }

  pruneSeriesPoints(points, minTimestamp) {
    while (points.length && points[0][0] < minTimestamp) {
      points.shift();
    }
    if (points.length > MAX_POINTS) {
      points.splice(0, points.length - MAX_POINTS);
    }
  }

  updateChartMetrics() {
    this.sampleCountLabel.textContent = String(this.totalSamples);
    this.seriesCountLabel.textContent = String(this.fieldSeries.size);
  }

  clearChart() {
    this.fieldSeries.clear();
    this.hiddenTelemetrySeries.clear();
    this.totalSamples = 0;
    this.lastTelemetryTimestamp = null;
    this.lastDataTimeLabel.textContent = "-";
    this.renderSeriesFilter([]);
    this.chart.setOption(
      { xAxis: { min: null, max: null }, legend: { data: [] }, series: [] },
      { replaceMerge: ["xAxis", "legend", "series"] }
    );
    this.updateChartMetrics();
    this.toast("曲线已清空");
  }

  resize() {
    this.chart.resize();
  }
}
