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

This starts:

- native MQTT: `tcp://127.0.0.1:1883`
- MQTT over WebSocket: `ws://127.0.0.1:9001`
- Dashboard HTTP: `http://127.0.0.1:8080`

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
./build/standard_mpc \
  --dashboard \
  --robot-id myrobot \
  --mqtt-host tcp://127.0.0.1:1883 \
  configs/standard3.yaml
```

or:

```bash
./build/auto_aim_debug_mpc \
  --dashboard \
  --robot-id myrobot \
  --mqtt-host tcp://127.0.0.1:1883 \
  configs/standard3.yaml
```

If the vision app runs in another Docker container, prefer host networking:

- `network_mode: host`
- vision app connects to `tcp://127.0.0.1:1883`
- browser opens `http://<host-ip>:8080`
- Dashboard WebSocket URL is `ws://<host-ip>:9001`

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

## Production Boundary

The production startup path no longer includes video-file runtime, virtual serial runtime, dev build container, or mock publisher entrypoints. Start the real vision app manually in its own environment.
