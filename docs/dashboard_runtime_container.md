# Dashboard Production Runtime

Dashboard production runtime is split into two independent services:

- `dashboard-net`: Mosquitto native MQTT, MQTT over WebSocket, and static HTTP Dashboard.
- vision app: `standard_mpc` or `auto_aim_debug_mpc`, started separately with real camera, gimbal, CAN, model, and config dependencies.

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

Start the real vision program outside the Dashboard container:

```bash
./build/standard_mpc --dashboard configs/standard3.yaml
```

or:

```bash
./build/auto_aim_debug_mpc --dashboard configs/standard3.yaml
```

Dashboard startup is controlled by the YAML `dashboard` block:

```yaml
dashboard:
  enabled: false
  robot_id: "myrobot"
  mqtt_host: "tcp://127.0.0.1:1883"
```

The merged main baseline keeps `enabled: false` by default so existing robot startup behavior does not change. For production Dashboard use, either change it to `true` in the deployed config or pass `--dashboard`.

Command line options still override YAML:

```bash
./build/standard_mpc --dashboard --robot-id hero --mqtt-host tcp://127.0.0.1:1883 configs/standard3.yaml
```

Passing `--dashboard` forces Dashboard on even if `dashboard.enabled` is `false`.

When the vision app runs on the same host namespace as the Dashboard network service, keep:

```text
tcp://127.0.0.1:1883
```

If the vision app runs on another machine or in another network namespace, use:

```text
tcp://主机IP:1883
```

If a future deployment only wants local access, manually change `docker-compose.dashboard.yml` to bind `127.0.0.1:端口:端口`. The current default intentionally allows LAN access.

## Extra Dependencies For Dashboard Feature

Vision app build/runtime needs:

- `libpaho-mqtt-dev`
- `libpaho-mqttpp-dev`
- `nlohmann-json3-dev`, unless the image already provides `nlohmann/json.hpp`

`dashboard-net` container needs:

- `mosquitto`
- static HTTP server; current image uses `python3 -m http.server`

Optional debug tools:

- `mosquitto-clients`

## Frontend Assets

`dashboard/index.html` currently loads `mqtt.js` and ECharts from CDN. If the production network has no external internet access, vendor these assets into `dashboard/vendor/` in a separate task.

The frontend is split into static files and is served directly by `python3 -m http.server`:

- `/index.html`
- `/css/dashboard.css`
- `/js/app.js`
- `/js/protocol.js`
- `/js/mqtt_transport.js`
- `/js/*_panel.js`

Frontend visual changes should normally touch `dashboard/index.html` and `dashboard/css/dashboard.css`. MQTT contract logic belongs in `dashboard/js/protocol.js`; UI modules should not hand-build MQTT topics or control payloads. MQTT.js connect, subscribe, publish, reconnect, and message dispatch are isolated in `dashboard/js/mqtt_transport.js`.

No npm, Vite, React, Vue, or build step is required for the current Dashboard runtime.

## Production Boundary

The current runtime has exactly two service shapes:

- Dashboard network service container.
- real vision app, started separately in the hardware environment.

The production startup path does not include image/video/MJPEG, Web terminal, mock runtime, video-source, hardwareless smoke, or mock publisher entrypoints. Start the real vision app manually in its own environment.
