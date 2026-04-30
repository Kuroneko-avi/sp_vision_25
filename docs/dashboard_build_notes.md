# Dashboard Build Notes

This document records the production Dashboard MQTT build boundary. The Dashboard network container and the vision app are separate processes connected by MQTT.

## CMake Integration

The root `CMakeLists.txt` includes `cmake/DashboardDeps.cmake`.

- `find_package(PahoMqttCpp QUIET)` probes optional C++ MQTT support.
- If Paho MQTT C++ is missing, normal non-dashboard vision targets still configure without the dashboard MQTT path.
- When `mqtt_bridge` exists, only `auto_aim_debug_mpc` links `mqtt_bridge` and `dashboard_params`, and only `auto_aim_debug_mpc` defines `SP_VISION_ENABLE_DASHBOARD_MQTT`.
- `standard_mpc` remains upstream behavior.
- The Dashboard-only libraries stay on `auto_aim_debug_mpc`.

## Vision App Dependencies Added By Dashboard

Install these only for the dashboard MQTT feature:

```bash
sudo apt update
sudo apt install -y libpaho-mqtt-dev libpaho-mqttpp-dev nlohmann-json3-dev
```

If the image already provides `nlohmann/json.hpp`, `nlohmann-json3-dev` is not an additional requirement.

## Dashboard Network Container Dependencies

`dashboard-net` only needs:

- Mosquitto for native MQTT and MQTT over WebSocket.
- A static HTTP server; current image uses `python3 -m http.server`.

Optional host-side debug tool:

```bash
sudo apt install -y mosquitto-clients
```

## Runtime Boundary

`dashboard-net` publishes no application telemetry and starts no vision program. Start `auto_aim_debug_mpc` manually with real hardware access when Dashboard is needed. Dashboard settings are read from `configs/standard3.yaml`:

```yaml
dashboard:
  enabled: false
  robot_id: "myrobot"
  mqtt_host: "tcp://127.0.0.1:1883"
```

The merged main baseline keeps `enabled: false` by default so existing robot startup behavior does not change. For production Dashboard use, either change it to `true` in the deployed config or pass `--dashboard` to `auto_aim_debug_mpc`.

Default startup without Dashboard remains unchanged:

```bash
./build/standard_mpc configs/standard3.yaml
```

or:

```bash
./build/auto_aim_debug_mpc configs/standard3.yaml
```

Dashboard startup uses `auto_aim_debug_mpc`:

```bash
./build/auto_aim_debug_mpc --dashboard --mqtt-host tcp://127.0.0.1:1883 configs/standard3.yaml
```

If the broker is on another LAN host, use that host address:

```bash
./build/auto_aim_debug_mpc --dashboard --mqtt-host tcp://Dashboard主机IP:1883 configs/standard3.yaml
```

CLI options override YAML. `--dashboard` forces enable, `--robot-id` overrides `dashboard.robot_id`, and `--mqtt-host` overrides `dashboard.mqtt_host`.

Do not run these commands on a machine without camera, gimbal, CAN, and model assets.

## Frontend Assets

`dashboard/index.html` loads local vendor assets from `dashboard/vendor/`: MQTT.js, ECharts, and GridStack. Versions and source URLs are recorded in `dashboard/vendor/README.md`; no npm build step is required.
