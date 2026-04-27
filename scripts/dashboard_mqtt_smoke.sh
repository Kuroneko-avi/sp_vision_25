#!/usr/bin/env bash
set -euo pipefail

HOST="${MQTT_HOST:-127.0.0.1}"
PORT="${MQTT_PORT:-1883}"
TOPIC="${MQTT_TOPIC:-sp_vision_25/dashboard/smoke}"
TIMEOUT_SECONDS="${MQTT_TIMEOUT_SECONDS:-5}"
PAYLOAD='{"source":"dashboard_mqtt_smoke","ok":true}'

TMP_DIR=""
SUB_PID=""

cleanup() {
  if [[ -n "${SUB_PID}" ]] && kill -0 "${SUB_PID}" >/dev/null 2>&1; then
    kill "${SUB_PID}" >/dev/null 2>&1 || true
    wait "${SUB_PID}" >/dev/null 2>&1 || true
  fi

  if [[ -n "${TMP_DIR}" ]]; then
    rm -rf "${TMP_DIR}"
  fi
}

trap cleanup EXIT

ok() {
  printf '[OK] %s\n' "$1"
}

fail() {
  printf '[FAIL] %s\n' "$1"
  exit 1
}

has_command() {
  command -v "$1" >/dev/null 2>&1
}

is_tcp_listening() {
  local port="$1"

  if has_command ss; then
    ss -ltn "sport = :${port}" 2>/dev/null | awk 'NR > 1 { found = 1 } END { exit found ? 0 : 1 }'
    return
  fi

  if has_command lsof; then
    lsof -nP -iTCP:"${port}" -sTCP:LISTEN >/dev/null 2>&1
    return
  fi

  if has_command netstat; then
    netstat -ltn 2>/dev/null | awk -v port=":${port}" '$4 ~ port "$" { found = 1 } END { exit found ? 0 : 1 }'
    return
  fi

  return 2
}

require_command() {
  local command_name="$1"

  if has_command "${command_name}"; then
    ok "已找到命令：${command_name}"
  else
    fail "缺少命令：${command_name}，请安装 mosquitto-clients"
  fi
}

wait_for_payload() {
  local output_file="$1"
  local deadline=$((SECONDS + TIMEOUT_SECONDS))

  while ((SECONDS < deadline)); do
    if grep -Fxq "${PAYLOAD}" "${output_file}"; then
      return 0
    fi
    sleep 0.2
  done

  return 1
}

main() {
  printf '== MQTT Dashboard 1883 smoke test ==\n'
  printf '目标 Broker：%s:%s\n' "${HOST}" "${PORT}"
  printf '测试 topic：%s\n' "${TOPIC}"

  require_command mosquitto_pub
  require_command mosquitto_sub

  if is_tcp_listening "${PORT}"; then
    ok "端口 ${PORT}/tcp 正在监听"
  else
    local status=$?
    if [[ "${status}" -eq 2 ]]; then
      fail "无法检查端口 ${PORT}/tcp：缺少 ss/lsof/netstat"
    else
      fail "端口 ${PORT}/tcp 未监听，请先启动 Mosquitto"
    fi
  fi

  TMP_DIR="$(mktemp -d)"
  local output_file="${TMP_DIR}/mqtt_payload.txt"

  : >"${output_file}"
  mosquitto_sub -h "${HOST}" -p "${PORT}" -t "${TOPIC}" -C 1 >"${output_file}" &
  SUB_PID="$!"

  sleep 0.4
  mosquitto_pub -h "${HOST}" -p "${PORT}" -t "${TOPIC}" -m "${PAYLOAD}"
  ok "已发布 JSON 测试消息"

  if wait_for_payload "${output_file}"; then
    ok "已收到原生 MQTT 回环消息：${PAYLOAD}"
  else
    fail "未在 ${TIMEOUT_SECONDS}s 内收到测试消息"
  fi

  printf 'smoke test 通过：本机 1883 原生 MQTT 可用。\n'
  printf '说明：1883/9001/8080 端口与 HTTP 检测请运行 scripts/dashboard_mqtt_check.sh。\n'
}

main "$@"
