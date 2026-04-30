# Dashboard Network Smoke

This smoke check validates only the production `dashboard-net` container. It does not start a vision program or telemetry publisher.

## Start

```bash
docker compose -f docker-compose.dashboard.yml up -d --build
```

Equivalent script:

```bash
scripts/dashboard_net_up.sh
```

The container exposes:

- Mosquitto native MQTT: `1883`
- Mosquitto MQTT over WebSocket: `9001`
- static Dashboard HTTP: `8080`

## Open Dashboard

```text
http://127.0.0.1:8080
```

Use these frontend connection values when running locally:

- Broker URL: `ws://127.0.0.1:9001`
- Robot ID: `myrobot`

The page will show telemetry and parameters after a separately started vision app publishes MQTT messages.

For a real LAN or cable setup, open `http://<host-ip>:8080` from the browser device and set Broker URL to `ws://<host-ip>:9001`.

Topology A: Dashboard service and `auto_aim_debug_mpc` run on the same robot or host.

```text
auto_aim_debug_mpc -> tcp://127.0.0.1:1883
browser device -> http://机器人IP:8080
browser MQTT WS -> ws://机器人IP:9001
```

Topology B: Dashboard service runs on another computer, while `auto_aim_debug_mpc` runs on the robot.

```text
auto_aim_debug_mpc -> tcp://Dashboard电脑IP:1883
browser device -> http://Dashboard电脑IP:8080
browser MQTT WS -> ws://Dashboard电脑IP:9001
```

`127.0.0.1` is only correct for the C++ process when the broker is in the same network namespace. Otherwise use the Dashboard host LAN IP.

## Check Ports

```bash
curl -I http://127.0.0.1:8080
ss -ltn '( sport = :1883 or sport = :9001 or sport = :8080 )'
docker compose -f docker-compose.dashboard.yml config
```

Optional native MQTT loopback if `mosquitto-clients` is installed:

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t sp_vision_25/dashboard/smoke -C 1 &
mosquitto_pub -h 127.0.0.1 -p 1883 -t sp_vision_25/dashboard/smoke -m '{"ok":true}'
```

## Stop

```bash
docker compose -f docker-compose.dashboard.yml down
```

Equivalent script:

```bash
scripts/dashboard_net_down.sh
```

## Production Boundary

`dashboard-net` does not:

- start `standard_mpc` or `auto_aim_debug_mpc`
- publish telemetry by itself
- access camera, serial, CAN, OpenVINO, or model files
- mount hardware devices
- serve from any C++ vision process
