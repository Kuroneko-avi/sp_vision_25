#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

docker compose -f docker-compose.dashboard.yml up -d --build --remove-orphans

printf 'Dashboard network is running.\n'
printf 'HTTP local: http://127.0.0.1:8080\n'
printf 'HTTP LAN: http://<host-ip>:8080\n'
printf 'MQTT: tcp://127.0.0.1:1883\n'
printf 'MQTT LAN: tcp://<host-ip>:1883\n'
printf 'WebSocket MQTT LAN: ws://<host-ip>:9001\n'
