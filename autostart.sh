#!/usr/bin/env bash
set -euo pipefail

APP_DIR="/home/jwj-rune/Downloads/sp_vision_25"
APP="./build/standard_mpc_fyt"
CONFIG="${1:-configs/infantry.yaml}"
STARTUP_DELAY="${STARTUP_DELAY:-8}"

cd "$APP_DIR"

mkdir -p logs
LOG_FILE="logs/autostart-$(date '+%Y-%m-%d_%H-%M-%S').log"

sleep "$STARTUP_DELAY"

if [[ ! -x "$APP" ]]; then
  echo "Executable not found or not executable: $APP" >> "$LOG_FILE"
  exit 1
fi

exec "$APP" "$CONFIG" >> "$LOG_FILE" 2>&1
