# Dashboard Production Runtime

Dashboard production runtime is split into two independent services:

- `dashboard-net`: Mosquitto native MQTT, MQTT over WebSocket, and static HTTP Dashboard.
- vision app: `auto_aim_debug_mpc`, started separately with real camera, gimbal, CAN, model, and config dependencies.

The two sides communicate only through MQTT topics.

## Start Dashboard Network

```bash
docker compose -f docker-compose.dashboard.yml up -d --build
```

Equivalent script:

```bash
scripts/dashboard_net_up.sh
```

This starts the Dashboard network service with LAN-accessible ports by default:

- native MQTT: `tcp://<host-ip>:1883`
- MQTT over WebSocket: `ws://<host-ip>:9001`
- Dashboard HTTP: `http://<host-ip>:8080`

Open the browser at:

```text
http://主机IP:8080
```

Use this frontend connection form:

```text
Broker URL: ws://主机IP:9001
Robot ID: myrobot
```

Stop it with:

```bash
docker compose -f docker-compose.dashboard.yml down
```

Equivalent script:

```bash
scripts/dashboard_net_down.sh
```

## Start Vision App

Start the real Dashboard-enabled vision program outside the Dashboard container:

```bash
./build/auto_aim_debug_mpc --dashboard --mqtt-host tcp://127.0.0.1:1883 configs/standard3.yaml
```

Dashboard startup is controlled by the YAML `dashboard` block:

```yaml
dashboard:
  enabled: false
  robot_id: "myrobot"
  mqtt_host: "tcp://127.0.0.1:1883"
```

The merged main baseline keeps `enabled: false` by default so existing robot startup behavior does not change. For production Dashboard use, either change it to `true` in the deployed config or pass `--dashboard` to `auto_aim_debug_mpc`.

Command line options still override YAML:

```bash
./build/auto_aim_debug_mpc --dashboard --robot-id hero --mqtt-host tcp://127.0.0.1:1883 configs/standard3.yaml
```

Passing `--dashboard` forces Dashboard on even if `dashboard.enabled` is `false`.

Topology A: Dashboard service and the vision app run on the same robot or host.

```text
auto_aim_debug_mpc -> tcp://127.0.0.1:1883
browser device -> http://机器人IP:8080
browser MQTT WS -> ws://机器人IP:9001
```

In this topology, `127.0.0.1` is correct for the C++ process because the MQTT broker is in the same host network namespace. The browser still uses the robot LAN IP over the cable or local network to fetch HTML/CSS/JS and connect to MQTT over WebSocket.

Topology B: Dashboard service runs on another computer, while the vision app runs on the robot.

```text
auto_aim_debug_mpc -> tcp://Dashboard电脑IP:1883
browser device -> http://Dashboard电脑IP:8080
browser MQTT WS -> ws://Dashboard电脑IP:9001
```

`127.0.0.1` is not the only production address. If the broker is not in the same network namespace as `auto_aim_debug_mpc`, use the Dashboard host LAN IP for `--mqtt-host`.

If a future deployment only wants local access, manually change `docker-compose.dashboard.yml` to bind `127.0.0.1:端口:端口`. The current default intentionally allows LAN access.

## Extra Dependencies For Dashboard Feature

Dashboard-enabled `auto_aim_debug_mpc` build/runtime needs:

- `libpaho-mqtt-dev`
- `libpaho-mqttpp-dev`
- `nlohmann-json3-dev`, unless the image already provides `nlohmann/json.hpp`

`dashboard-net` container needs:

- `mosquitto`
- static HTTP server; current image uses `python3 -m http.server`

Optional debug tools:

- `mosquitto-clients`

## Frontend Assets

`dashboard/index.html` loads local vendor assets instead of external CDN:

- `/vendor/mqtt/mqtt.min.js`
- `/vendor/echarts/echarts.min.js`
- `/vendor/gridstack/gridstack-all.js`
- `/vendor/gridstack/gridstack.min.css`

Versions and source URLs are recorded in `dashboard/vendor/README.md`.

The frontend is a lightweight panel workspace served directly by `python3 -m http.server`:

- `/index.html`
- `/css/dashboard.css`
- `/js/app.js`
- `/js/core/protocol.js`
- `/js/core/mqtt_transport.js`
- `/js/core/store.js`
- `/js/core/panel_registry.js`
- `/js/core/layout_manager.js`
- `/js/panels/*.js`

The workspace borrows the Foxglove/rqt idea of a registry-driven panel surface without adopting those platforms. GridStack is the local vendor layout component for draggable and resizable panels.

Frontend visual changes should normally touch `dashboard/css/dashboard.css` and `dashboard/js/panels/*`. MQTT contract logic belongs in `dashboard/js/core/protocol.js` and must stay aligned with `docs/dashboard_mqtt_protocol.md`; panel modules should not hand-build MQTT topics or call MQTT.js directly. MQTT.js connect, subscribe, publish, reconnect, and message dispatch are isolated in `dashboard/js/core/mqtt_transport.js`.

No npm, Vite, React, Vue, or build step is required for the current Dashboard runtime.

## Production Boundary

The current runtime has exactly two service shapes:

- Dashboard network service container.
- real `auto_aim_debug_mpc`, started separately in the hardware environment.

The production startup path does not include image/video/MJPEG, Web terminal, mock runtime, video-source, hardwareless smoke, or mock publisher entrypoints. The vision program is only an MQTT native client on `1883`; it does not serve HTML, CSS, JS, or any HTTP endpoint. `standard_mpc` remains the normal non-Dashboard business entry.
