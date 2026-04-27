#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

ROBOT_ID="${ROBOT_ID:-myrobot}"
MQTT_HOST="${MQTT_HOST:-tcp://127.0.0.1:1883}"
VIDEO_SOURCE="${VIDEO_SOURCE:-assets/demo/demo.avi}"
CONFIG_PATH="${CONFIG_PATH:-configs/standard3.yaml}"

fail() {
  echo "[hardwareless-smoke] ERROR: $*" >&2
  exit 1
}

cleanup() {
  docker compose -f docker-compose.dashboard.yml down >/dev/null 2>&1 || true
}
trap cleanup EXIT

wait_for_http() {
  for _ in $(seq 1 30); do
    if curl -fsS http://127.0.0.1:8080 >/dev/null; then
      return 0
    fi
    sleep 1
  done
  return 1
}

docker compose -f docker-compose.dashboard.yml up -d --build
wait_for_http || fail "dashboard http://127.0.0.1:8080 is not reachable"

docker compose -f docker-compose.dev.yml run --rm sp-vision-dev bash -s -- \
  "${ROBOT_ID}" "${MQTT_HOST}" "${VIDEO_SOURCE}" "${CONFIG_PATH}" <<'CONTAINER_SMOKE'
set -euo pipefail

ROBOT_ID="$1"
MQTT_HOST="$2"
VIDEO_SOURCE="$3"
CONFIG_PATH="$4"

APP_PID=""
SUB_PID=""
GIMBAL_PID=""

fail() {
  echo "[hardwareless-smoke:container] ERROR: $*" >&2
  exit 1
}

cleanup() {
  [ -n "${APP_PID}" ] && kill "${APP_PID}" 2>/dev/null || true
  [ -n "${SUB_PID}" ] && kill "${SUB_PID}" 2>/dev/null || true
  [ -n "${GIMBAL_PID}" ] && kill "${GIMBAL_PID}" 2>/dev/null || true
  rm -f /tmp/dashboard_hardwareless_topics.log /tmp/dashboard_hardwareless_app.log
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
timeout 35s mosquitto_sub -h 127.0.0.1 -t "${ROBOT_ID}/#" -v \
  > /tmp/dashboard_hardwareless_topics.log &
SUB_PID=$!

timeout 30s ./build/auto_aim_debug_mpc \
  --dashboard \
  --robot-id "${ROBOT_ID}" \
  --mqtt-host "${MQTT_HOST}" \
  --mock-runtime \
  --video-source "${VIDEO_SOURCE}" \
  --video-loop \
  "${CONFIG_PATH}" \
  > /tmp/dashboard_hardwareless_app.log 2>&1 &
APP_PID=$!

wait_for_topic() {
  local pattern="$1"
  local label="$2"
  for _ in $(seq 1 80); do
    if grep -qE "${pattern}" /tmp/dashboard_hardwareless_topics.log 2>/dev/null; then
      echo "[hardwareless-smoke:container] found ${label}"
      return 0
    fi
    if ! kill -0 "${APP_PID}" 2>/dev/null; then
      tail -n 80 /tmp/dashboard_hardwareless_app.log >&2 || true
      fail "auto_aim_debug_mpc exited before ${label}"
    fi
    sleep 0.25
  done
  tail -n 80 /tmp/dashboard_hardwareless_app.log >&2 || true
  fail "missing ${label}"
}

publish_cmd_and_expect_ack() {
  local request_id="$1"
  local command="$2"
  local ok_expected="$3"
  local ack_file="/tmp/dashboard_hardwareless_ack_${request_id}.log"
  rm -f "${ack_file}"

  timeout 8s mosquitto_sub -h 127.0.0.1 -t "${ROBOT_ID}/control/ack" -C 1 -v \
    > "${ack_file}" &
  local ack_pid=$!
  sleep 0.3

  local timestamp
  timestamp="$(date +%s%3N)"
  mosquitto_pub -h 127.0.0.1 -t "${ROBOT_ID}/control/cmd" \
    -m "{\"request_id\":\"${request_id}\",\"command\":\"${command}\",\"timestamp\":${timestamp},\"args\":{}}"

  if ! wait "${ack_pid}"; then
    cat "${ack_file}" >&2 || true
    fail "missing ack for ${command}"
  fi

  grep -q "\"request_id\":\"${request_id}\"" "${ack_file}" || fail "ack request_id mismatch for ${command}"
  grep -q "\"ok\":${ok_expected}" "${ack_file}" || fail "ack ok mismatch for ${command}"
  cat "${ack_file}" >> /tmp/dashboard_hardwareless_topics.log
  echo "[hardwareless-smoke:container] ack ${command} ok=${ok_expected}"
}

wait_for_topic "^${ROBOT_ID}/params/schema " "params/schema"
wait_for_topic "^${ROBOT_ID}/params/current " "params/current"
wait_for_topic "^${ROBOT_ID}/data " "data"

publish_cmd_and_expect_ack "smoke-republish" "republish_params" "true"
publish_cmd_and_expect_ack "smoke-stop" "stop_dashboard" "true"
publish_cmd_and_expect_ack "smoke-start" "start_dashboard" "true"
publish_cmd_and_expect_ack "smoke-unknown" "unknown_command" "false"
wait_for_topic "^${ROBOT_ID}/control/ack " "control/ack"

grep -q "MQTT Dashboard enabled" /tmp/dashboard_hardwareless_app.log || fail "dashboard init log missing"
echo "[hardwareless-smoke:container] hardwareless dashboard smoke passed"
CONTAINER_SMOKE

echo "[hardwareless-smoke] passed"
