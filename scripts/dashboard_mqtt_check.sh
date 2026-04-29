#!/usr/bin/env bash
set -euo pipefail

FAIL_COUNT=0

ok() {
  printf '[OK] %s\n' "$1"
}

warn() {
  printf '[WARN] %s\n' "$1"
}

info() {
  printf '[INFO] %s\n' "$1"
}

fail() {
  printf '[FAIL] %s\n' "$1"
  FAIL_COUNT=$((FAIL_COUNT + 1))
}

has_command() {
  command -v "$1" >/dev/null 2>&1
}

check_command() {
  local cmd="$1"

  if has_command "${cmd}"; then
    ok "${cmd} is available"
  else
    warn "missing ${cmd}; optional for Docker Dashboard network check"
  fi
}

check_file_contains() {
  local file="$1"
  local pattern="$2"
  local label="$3"

  if grep -q "${pattern}" "${file}"; then
    ok "${label}"
  else
    warn "${label}"
  fi
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

check_required_port() {
  local port="$1"
  local label="$2"

  if is_tcp_listening "${port}"; then
    ok "${label} port ${port}/tcp is listening"
  else
    local status=$?
    if [[ "${status}" -eq 2 ]]; then
      fail "cannot check ${label} port ${port}/tcp: missing ss/lsof/netstat"
    else
      fail "${label} port ${port}/tcp is not listening"
    fi
  fi
}

main() {
  printf '== Dashboard network check ==\n'

  printf 'Checking Docker Dashboard network service ports and HTTP endpoint.\n'
  check_command mosquitto
  check_command mosquitto_pub
  check_command mosquitto_sub

  local dashboard_conf="/etc/mosquitto/conf.d/dashboard.conf"
  if [[ -f "${dashboard_conf}" ]]; then
    ok "${dashboard_conf} exists"
    check_file_contains "${dashboard_conf}" "listener 9001" \
      "${dashboard_conf} contains 'listener 9001'"
    check_file_contains "${dashboard_conf}" "protocol websockets" \
      "${dashboard_conf} contains 'protocol websockets'"
  else
    info "${dashboard_conf} not found; skipped optional system Mosquitto config check"
  fi

  check_required_port 1883 "native MQTT"
  check_required_port 9001 "WebSocket MQTT"
  check_required_port 8080 "HTTP Dashboard"

  if has_command curl; then
    if curl --noproxy "*" -fsSI http://127.0.0.1:8080 >/dev/null; then
      ok "HTTP Dashboard responds at http://127.0.0.1:8080"
    else
      fail "HTTP Dashboard does not respond at http://127.0.0.1:8080"
    fi
  else
    fail "cannot check HTTP Dashboard: missing curl"
  fi

  if [[ "${FAIL_COUNT}" -eq 0 ]]; then
    printf 'Dashboard network check passed.\n'
  else
    printf 'Dashboard network check failed with %d issue(s).\n' "${FAIL_COUNT}"
    exit 1
  fi
}

main "$@"
