import { parseTelemetryPayload } from "./protocol.js";
import { formatTime, toTimestampMs } from "./ui_state.js";

const MAX_POINTS = 200;
const TELEMETRY_WINDOW_MS = 30000;

export class TelemetryPanel {
  constructor({ onWarn, onToast }) {
    this.onWarn = onWarn;
    this.onToast = onToast;
    this.chart = echarts.init(document.getElementById("chart"));
    this.sampleCountLabel = document.getElementById("sample-count");
    this.seriesCountLabel = document.getElementById("series-count");
    this.lastDataTimeLabel = document.getElementById("last-data-time");
    this.telemetrySourceLabel = document.getElementById("telemetry-source");
    this.seriesFilter = document.getElementById("series-filter");
    this.clearChartButton = document.getElementById("clear-chart");
    this.totalSamples = 0;
    this.lastTelemetryTimestamp = null;
    this.fieldSeries = new Map();
    this.hiddenTelemetrySeries = new Set();

    this.initChart();
    this.updateChartMetrics();
    window.addEventListener("resize", () => this.chart.resize());
    this.clearChartButton.addEventListener("click", () => this.clearChart());
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

  handleTelemetry(message) {
    const telemetry = parseTelemetryPayload(message);
    if (!telemetry.values) {
      this.onWarn?.("telemetry payload missing values/fields object");
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
    this.onToast?.("曲线已清空");
  }
}
