#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

ROBOT_ID="${ROBOT_ID:-myrobot}"
MQTT_HOST="${MQTT_HOST:-tcp://127.0.0.1:1883}"
VIDEO_SOURCE="${VIDEO_SOURCE:-assets/demo/demo.avi}"
CONFIG_PATH="${CONFIG_PATH:-configs/standard3.yaml}"
KEEP_DASHBOARD="${KEEP_DASHBOARD:-0}"
RUN_CONTAINER_NAME="${RUN_CONTAINER_NAME:-sp_vision_25-hardwareless-run}"

fail() {
  echo "[hardwareless-run] ERROR: $*" >&2
  exit 1
}

cleanup() {
  docker rm -f "${RUN_CONTAINER_NAME}" >/dev/null 2>&1 || true
  if [ "${KEEP_DASHBOARD}" != "1" ]; then
    docker compose -f docker-compose.dashboard.yml down --remove-orphans >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT INT TERM

wait_for_http() {
  for _ in $(seq 1 30); do
    if curl -fsS http://127.0.0.1:8080 >/dev/null; then
      return 0
    fi
    sleep 1
  done
  return 1
}

docker compose -f docker-compose.dashboard.yml up -d --build --remove-orphans
wait_for_http || fail "dashboard http://127.0.0.1:8080 is not reachable"

echo "[hardwareless-run] dashboard: http://127.0.0.1:8080"
echo "[hardwareless-run] robot_id=${ROBOT_ID} mqtt_host=${MQTT_HOST}"
echo "[hardwareless-run] video_source=${VIDEO_SOURCE}"
echo "[hardwareless-run] press Ctrl+C to stop"

docker rm -f "${RUN_CONTAINER_NAME}" >/dev/null 2>&1 || true
COMPOSE_IGNORE_ORPHANS=true docker compose -f docker-compose.dev.yml run \
  --name "${RUN_CONTAINER_NAME}" --rm sp-vision-dev bash -s -- \
  "${ROBOT_ID}" "${MQTT_HOST}" "${VIDEO_SOURCE}" "${CONFIG_PATH}" <<'CONTAINER_RUN'
set -euo pipefail

ROBOT_ID="$1"
MQTT_HOST="$2"
VIDEO_SOURCE="$3"
CONFIG_PATH="$4"

APP_PID=""
GIMBAL_PID=""
APP_LOG="/tmp/dashboard_hardwareless_run_app.log"

cleanup() {
  [ -n "${APP_PID}" ] && kill "${APP_PID}" 2>/dev/null || true
  [ -n "${GIMBAL_PID}" ] && kill "${GIMBAL_PID}" 2>/dev/null || true
}
trap cleanup EXIT

python3 -u - <<'PY' &
import os
import pty
import select
import struct
import time
import tty


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return crc & 0xFFFF


master, slave = pty.openpty()
tty.setraw(master)
tty.setraw(slave)

try:
    os.unlink("/dev/gimbal")
except FileNotFoundError:
    pass
os.symlink(os.ttyname(slave), "/dev/gimbal")

base = struct.pack(
    "<2sB9fHH",
    b"CB",
    0,
    1.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    15.0,
    0,
    0,
)
frame_without_crc = base[:-2]
frame = frame_without_crc + struct.pack("<H", crc16(frame_without_crc))

while True:
    while select.select([master], [], [], 0)[0]:
        os.read(master, 4096)
    os.write(master, frame)
    time.sleep(0.01)
PY
GIMBAL_PID=$!

sleep 1

./build/auto_aim_debug_mpc \
  --dashboard \
  --robot-id "${ROBOT_ID}" \
  --mqtt-host "${MQTT_HOST}" \
  --mock-runtime \
  --video-source "${VIDEO_SOURCE}" \
  --video-loop \
  "${CONFIG_PATH}" \
  > "${APP_LOG}" 2>&1 &
APP_PID=$!

echo "[hardwareless-run:container] app log: ${APP_LOG}"
echo "[hardwareless-run:container] auto_aim_debug_mpc is running"

if ! wait "${APP_PID}"; then
  status=$?
  tail -n 80 "${APP_LOG}" >&2 || true
  exit "${status}"
fi
CONTAINER_RUN
