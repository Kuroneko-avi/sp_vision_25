# MQTT Dashboard Changelog And Usage

本文面向合并评审和后续使用者，说明本分支新增的 MQTT Dashboard 功能、依赖、启动方式和生产边界。

## 变更摘要

- 新增浏览器 Dashboard：`dashboard/index.html`。
- Dashboard 前端已从静态多文件升级为轻量 panel 工作台：使用 GridStack 管理可拖拽、可缩放 panel，仍由容器内静态 HTTP server 直接 serve，不需要 npm build。
- 新增独立 Dashboard 网络服务容器：`docker-compose.dashboard.yml`、`docker/dashboard/*`。
- 新增 MQTT 通信层：`tools/mqtt_bridge.*`、`tools/dashboard_mqtt_contract.hpp`。
- 新增 Dashboard 参数模型：`tools/dashboard_params.*`、`tools/dashboard_config.*`。
- `standard_mpc` 与 `auto_aim_debug_mpc` 支持通过 MQTT 发布 telemetry、log、params/schema、params/current，并消费 control/param、control/cmd。
- `configs/standard3.yaml` 新增 `dashboard` 配置段。

本功能不包含图像流、视频流、MJPEG 或 Web terminal。

## 生产部署形态

生产环境拆成两个独立服务：

1. Dashboard 网络服务容器
   - Mosquitto native MQTT：`1883`
   - MQTT over WebSocket：`9001`
   - HTTP Dashboard：`8080`
   - 默认开放到主机全部网卡，允许局域网访问。

2. 视觉主程序
   - `standard_mpc`
   - `auto_aim_debug_mpc`
   - 在真实硬件环境中单独启动，通过 MQTT 连接 Dashboard 网络服务。

两者只通过 MQTT 通信。Dashboard 容器不启动视觉主程序，也不启动 mock publisher。

## 新增依赖

视觉主程序构建或运行 Dashboard MQTT 功能需要：

```bash
sudo apt update
sudo apt install -y libpaho-mqtt-dev libpaho-mqttpp-dev nlohmann-json3-dev
```

如果基础镜像已经提供 `nlohmann/json.hpp`，则 `nlohmann-json3-dev` 不是额外依赖。

Dashboard 网络服务容器内部依赖：

- `mosquitto`
- `python3`，用于 `python3 -m http.server` 提供静态 HTTP 服务。

可选调试工具：

```bash
sudo apt install -y mosquitto-clients
```

前端依赖已 vendor 到本地 `dashboard/vendor/`：

- `mqtt.js`：`dashboard/vendor/mqtt/mqtt.min.js`
- ECharts：`dashboard/vendor/echarts/echarts.min.js`
- GridStack：`dashboard/vendor/gridstack/gridstack-all.js`、`dashboard/vendor/gridstack/gridstack.min.css`

版本和来源记录在 `dashboard/vendor/README.md`。当前前端不再依赖外部 CDN。

前端装修优先修改：

- `dashboard/css/dashboard.css`：视觉样式。
- `dashboard/js/panels/*`：具体 panel 内容和交互。

当前借鉴 Foxglove/rqt 的 panel registry 思路，核心结构为：

- `dashboard/js/core/panel_registry.js`：注册 panel id、标题、默认尺寸和 mount 方法。
- `dashboard/js/core/layout_manager.js`：初始化 GridStack 并挂载 panel 容器。
- `dashboard/js/core/store.js`：保存连接状态、最新 telemetry、params、ack、log 和 Raw MQTT 消息。
- `dashboard/js/panels/*`：telemetry、params、commands、ack、logs、Raw MQTT 等独立面板。

MQTT 契约逻辑集中在 `dashboard/js/core/protocol.js`，包括 topic、QoS、telemetry 解析、参数 payload 校验和 control payload 构造。Panel 文件不要绕过 `protocol.js` 直接拼 topic 或 control payload。MQTT.js 连接、订阅、发布和消息分发集中在 `dashboard/js/core/mqtt_transport.js`。

当前前端仍是原生静态资源，不引入 Vite、React、Vue 或 npm build。

## 配置方式

`configs/standard3.yaml` 新增：

```yaml
dashboard:
  enabled: false
  robot_id: "myrobot"
  mqtt_host: "tcp://127.0.0.1:1883"
```

默认 `enabled: false`，目的是合入 main 后不改变原有主程序启动行为。

生产启用方式二选一：

1. 修改部署用 YAML：

```yaml
dashboard:
  enabled: true
  robot_id: "myrobot"
  mqtt_host: "tcp://127.0.0.1:1883"
```

2. 启动时使用 CLI 覆盖：

```bash
./build/standard_mpc --dashboard --robot-id myrobot --mqtt-host tcp://127.0.0.1:1883 configs/standard3.yaml
```

CLI 优先级高于 YAML：

- `--dashboard`：强制启用 Dashboard。
- `--robot-id hero` 或 `--robot-id=hero`：覆盖 `dashboard.robot_id`。
- `--mqtt-host tcp://...` 或 `--mqtt-host=tcp://...`：覆盖 `dashboard.mqtt_host`。

如果 Dashboard 启用但 MQTT 初始化或连接失败，主程序会记录 warning，并继续原视觉业务路径，不直接退出。

## 启动方式

先启动 Dashboard 网络服务：

```bash
./scripts/dashboard_net_up.sh
```

等价命令：

```bash
docker compose -f docker-compose.dashboard.yml up -d --build
```

浏览器访问：

```text
http://主机IP:8080
```

前端填写：

```text
Broker URL: ws://主机IP:9001
Robot ID: myrobot
```

再启动真实视觉主程序：

```bash
./build/standard_mpc --dashboard configs/standard3.yaml
```

或：

```bash
./build/auto_aim_debug_mpc --dashboard configs/standard3.yaml
```

如果 YAML 中 `dashboard.enabled` 保持 `false`，则使用：

```bash
./build/standard_mpc --dashboard configs/standard3.yaml
```

停止 Dashboard 网络服务：

```bash
./scripts/dashboard_net_down.sh
```

## Docker 网络建议

Dashboard 网络服务容器默认允许局域网访问：

```text
http://主机IP:8080
ws://主机IP:9001
```

视觉主程序与 Dashboard 网络服务在同一主机网络命名空间时连接：

```text
tcp://127.0.0.1:1883
```

如果视觉主程序在另一台机器或另一网络命名空间中，连接：

```text
tcp://主机IP:1883
```

如果未来只想本机访问，可以手动把 compose 改为 `127.0.0.1:端口:端口`；当前默认不要这样做。

由于当前不传输图像或视频，只传 telemetry、log、params、control 和 ack，两容器拆分带来的本机 MQTT 传输开销可以忽略。

## 手动验收

检查 Dashboard 网络服务：

```bash
./scripts/dashboard_net_up.sh
./scripts/dashboard_mqtt_check.sh
curl --noproxy "*" -I http://127.0.0.1:8080
./scripts/dashboard_net_down.sh
```

有真实硬件和完整依赖时，构建并启动：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target standard_mpc auto_aim_debug_mpc dashboard_params_test -j$(nproc)
./scripts/dashboard_net_up.sh
./build/standard_mpc --dashboard configs/standard3.yaml
```

浏览器中确认：

- telemetry 曲线持续更新。
- params/schema 与 params/current 可见。
- 可发送 `stop_dashboard`、`start_dashboard`、`republish_params`。
- control/ack 正常返回。

## 合并注意事项

- 合入 main 前应在具备 CMake、OpenVINO、Paho MQTT C++、nlohmann_json 的环境中完成 C++ 构建验证。
- 不应重新引入无硬件开发路径，例如 `--mock-runtime`、`--video-source`、`--video-loop`、mock publisher 或 hardwareless smoke 脚本。
- 真实相机、串口和 CAN 验证应在硬件环境中单独完成。
