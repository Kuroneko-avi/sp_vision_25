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
