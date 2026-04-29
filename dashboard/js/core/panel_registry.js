import { AckPanel } from "../panels/ack_panel.js";
import { CommandPanel } from "../panels/command_panel.js";
import { LogViewerPanel } from "../panels/log_viewer_panel.js";
import { ParamsEditorPanel } from "../panels/params_editor_panel.js";
import { RawMqttPanel } from "../panels/raw_mqtt_panel.js";
import { TelemetryPlotPanel } from "../panels/telemetry_plot_panel.js";

export class PanelRegistry {
  constructor() {
    this.panels = [];
  }

  register(definition) {
    this.panels.push(definition);
  }

  list() {
    return this.panels;
  }
}

export function createDefaultPanelRegistry() {
  const registry = new PanelRegistry();
  registry.register({
    id: "telemetry",
    title: "遥测曲线",
    subtitle: "PlotJuggler 风格的数值趋势面板，优先读取 values。",
    layout: { x: 0, y: 0, w: 8, h: 6 },
    mount: (root, context) => new TelemetryPlotPanel(root, context)
  });
  registry.register({
    id: "params",
    title: "参数控制",
    subtitle: "基于 params/schema 自动生成编辑器。",
    layout: { x: 8, y: 0, w: 4, h: 6 },
    mount: (root, context) => new ParamsEditorPanel(root, context)
  });
  registry.register({
    id: "commands",
    title: "控制命令",
    subtitle: "只发布正式 control/cmd envelope。",
    layout: { x: 0, y: 6, w: 4, h: 4 },
    mount: (root, context) => new CommandPanel(root, context)
  });
  registry.register({
    id: "ack",
    title: "控制回执",
    subtitle: "显示最近 control/ack。",
    layout: { x: 4, y: 6, w: 4, h: 4 },
    mount: (root, context) => new AckPanel(root, context)
  });
  registry.register({
    id: "logs",
    title: "运行日志",
    subtitle: "最多保留 500 条系统和机器人日志。",
    layout: { x: 8, y: 6, w: 4, h: 4 },
    mount: (root, context) => new LogViewerPanel(root, context)
  });
  registry.register({
    id: "raw_mqtt",
    title: "Raw MQTT",
    subtitle: "最近 200 条原始订阅消息，只读调试视图。",
    layout: { x: 0, y: 10, w: 12, h: 4 },
    mount: (root, context) => new RawMqttPanel(root, context)
  });
  return registry;
}
