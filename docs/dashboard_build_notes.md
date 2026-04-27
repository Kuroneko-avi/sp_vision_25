# Dashboard Build Notes

This document records the production Dashboard MQTT build boundary. The Dashboard network container and the vision app are separate processes connected by MQTT.

## CMake Integration

The root `CMakeLists.txt` includes `cmake/DashboardDeps.cmake`.

- `find_package(PahoMqttCpp QUIET)` probes optional C++ MQTT support.
- If Paho MQTT C++ is missing, normal non-dashboard vision targets still configure without the dashboard MQTT path.
- When `mqtt_bridge` exists, `standard_mpc` and `auto_aim_debug_mpc` link `mqtt_bridge` and `dashboard_params`, and define `SP_VISION_ENABLE_DASHBOARD_MQTT`.

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

`dashboard-net` publishes no application telemetry and starts no vision program. Start `standard_mpc` or `auto_aim_debug_mpc` manually with real hardware access. Dashboard settings are read from `configs/standard3.yaml`:

```yaml
dashboard:
  enabled: true
  robot_id: "myrobot"
  mqtt_host: "tcp://127.0.0.1:1883"
```

Recommended startup:

```bash
./build/standard_mpc configs/standard3.yaml
```

or:

```bash
./build/auto_aim_debug_mpc configs/standard3.yaml
```

CLI options override YAML. `--dashboard` forces enable, `--robot-id` overrides `dashboard.robot_id`, and `--mqtt-host` overrides `dashboard.mqtt_host`.

Do not run these commands on a machine without camera, gimbal, CAN, and model assets.

## Frontend Assets

`dashboard/index.html` currently loads `mqtt.js` and ECharts from CDN. Production networks without external internet access should vendor those assets into `dashboard/vendor/` in a separate task.
