#!/usr/bin/env bash
set -euo pipefail

PIDS=()

terminate_children() {
  trap - SIGINT SIGTERM

  for pid in "${PIDS[@]}"; do
    if kill -0 "${pid}" >/dev/null 2>&1; then
      kill "${pid}" >/dev/null 2>&1 || true
    fi
  done

  for pid in "${PIDS[@]}"; do
    wait "${pid}" >/dev/null 2>&1 || true
  done
}

handle_signal() {
  printf '[entrypoint] received stop signal, terminating children\n'
  terminate_children
  exit 143
}

start_process() {
  local name="$1"
  shift

  printf '[entrypoint] starting %s: %s\n' "${name}" "$*"
  "$@" &
  PIDS+=("$!")
}

monitor_processes() {
  while true; do
    for pid in "${PIDS[@]}"; do
      if ! kill -0 "${pid}" >/dev/null 2>&1; then
        set +e
        wait "${pid}"
        local status="$?"
        set -e
        printf '[entrypoint] child process %s exited with status %s\n' "${pid}" "${status}"
        terminate_children
        return "${status}"
      fi
    done
    sleep 1
  done
}

main() {
  trap handle_signal SIGINT SIGTERM

  start_process mosquitto mosquitto -c /etc/mosquitto/mosquitto.conf
  start_process http python3 -m http.server 8080 --bind 0.0.0.0 --directory /app/dashboard

  monitor_processes
}

main "$@"
