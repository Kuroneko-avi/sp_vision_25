#!/usr/bin/env bash
set -euo pipefail

CONFIG_FILE="/etc/mosquitto/conf.d/dashboard.conf"
REQUIRED_COMMANDS=(mosquitto mosquitto_pub mosquitto_sub)
FAIL_COUNT=0

ok() {
  printf '[OK] %s\n' "$1"
}

warn() {
  printf '[WARN] %s\n' "$1"
}

fail() {
  printf '[FAIL] %s\n' "$1"
  FAIL_COUNT=$((FAIL_COUNT + 1))
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

check_command() {
  local command_name="$1"

  if has_command "${command_name}"; then
    ok "已找到命令：${command_name}"
  else
    fail "缺少命令：${command_name}，请安装 mosquitto/mosquitto-clients"
  fi
}

check_required_port() {
  local port="$1"
  local label="$2"

  if is_tcp_listening "${port}"; then
    ok "${label} 端口 ${port}/tcp 正在监听"
  else
    local status=$?
    if [[ "${status}" -eq 2 ]]; then
      fail "无法检查 ${label} 端口 ${port}/tcp：缺少 ss/lsof/netstat"
    else
      fail "${label} 端口 ${port}/tcp 未监听"
    fi
  fi
}

check_reserved_port() {
  local port="$1"
  local label="$2"

  if is_tcp_listening "${port}"; then
    warn "${label} 端口 ${port}/tcp 当前已被占用；确认是否为后续 C++ HTTP server"
  else
    local status=$?
    if [[ "${status}" -eq 2 ]]; then
      warn "无法检查 ${label} 端口 ${port}/tcp：缺少 ss/lsof/netstat"
    else
      ok "${label} 端口 ${port}/tcp 当前未监听，保留给后续 C++ HTTP server"
    fi
  fi
}

check_config() {
  if [[ ! -f "${CONFIG_FILE}" ]]; then
    fail "未找到配置文件：${CONFIG_FILE}"
    return
  fi

  ok "已找到配置文件：${CONFIG_FILE}"

  if grep -Eq '^[[:space:]]*listener[[:space:]]+9001([[:space:]]|$)' "${CONFIG_FILE}"; then
    ok "配置包含 WebSocket listener：9001"
  else
    fail "配置缺少 listener 9001"
  fi

  if grep -Eq '^[[:space:]]*protocol[[:space:]]+websockets([[:space:]]|$)' "${CONFIG_FILE}"; then
    ok "配置包含 protocol websockets"
  else
    fail "配置缺少 protocol websockets"
  fi
}

main() {
  printf '== MQTT Dashboard Broker 检测 ==\n'

  for command_name in "${REQUIRED_COMMANDS[@]}"; do
    check_command "${command_name}"
  done

  check_required_port 1883 "原生 MQTT"
  check_required_port 9001 "WebSocket MQTT"
  check_reserved_port 8080 "HTTP server"
  check_config

  if [[ "${FAIL_COUNT}" -eq 0 ]]; then
    printf '检测完成：Broker 基础项通过。\n'
  else
    printf '检测完成：发现 %d 个失败项，请按 docs/dashboard_mqtt_broker.md 修复。\n' "${FAIL_COUNT}"
    exit 1
  fi
}

main "$@"
