#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

docker compose -f docker-compose.dev.yml build
docker compose -f docker-compose.dev.yml run --rm sp-vision-dev \
  bash -lc 'cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target auto_aim_debug_mpc standard_mpc mqtt_bridge_smoke dashboard_params_test -j"$(nproc)"'
