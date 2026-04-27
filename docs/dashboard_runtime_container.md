# Dashboard Runtime Container

This container is for building the C++ dashboard integration and running local MQTT smoke checks without installing dependencies on the WSL host.

## Build

```bash
scripts/dashboard_runtime_build.sh
```

Equivalent manual commands:

```bash
docker compose -f docker-compose.dev.yml build
docker compose -f docker-compose.dev.yml run --rm sp-vision-dev \
  bash -lc 'cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target auto_aim_debug_mpc standard_mpc mqtt_bridge_smoke dashboard_params_test -j"$(nproc)"'
```

The repository is mounted at `/app`; the image does not copy the source tree as the primary build input.

## Run Shell

```bash
scripts/dashboard_runtime_run.sh bash
```

The dev service uses host networking so `tcp://127.0.0.1:1883` reaches the dashboard smoke broker started on the host.

## Local Smoke

```bash
docker compose -f docker-compose.dashboard.yml up -d --build
scripts/dashboard_runtime_run.sh bash -lc 'MQTT_SERVER_URI=tcp://127.0.0.1:1883 MQTT_ROBOT_ID=myrobot ./build/mqtt_bridge_smoke'
docker compose -f docker-compose.dashboard.yml down
```

## Hardwareless Control Smoke

```bash
scripts/dashboard_hardwareless_smoke.sh
```

The script starts the dashboard broker/http container, creates a virtual `/dev/gimbal` inside the dev container, runs `auto_aim_debug_mpc` with `--mock-runtime --video-source assets/demo/demo.avi --video-loop`, and verifies `data`, `params/schema`, `params/current`, and `control/ack`.

For manual browser inspection, use the long-running hardwareless runtime:

```bash
scripts/dashboard_hardwareless_run.sh
```

It starts the same broker/http stack and virtual `/dev/gimbal`, then keeps `auto_aim_debug_mpc` running with the demo video loop until Ctrl+C. Override inputs with environment variables:

```bash
ROBOT_ID=myrobot VIDEO_SOURCE=assets/demo/demo.avi scripts/dashboard_hardwareless_run.sh
```

The parameter panel is schema-driven. MPC entrypoints publish a catalog derived from `configs/standard3.yaml`: hot Planner/Buff Aimer fields are editable, while the remaining scalar, array, matrix, and calibration values are displayed as read-only restart-required configuration.

Hardware entrypoints can be launched from the same container:

```bash
scripts/dashboard_runtime_run.sh ./build/auto_aim_debug_mpc --dashboard --robot-id myrobot --mqtt-host tcp://127.0.0.1:1883 configs/standard3.yaml
scripts/dashboard_runtime_run.sh ./build/standard_mpc --dashboard --robot-id myrobot --mqtt-host tcp://127.0.0.1:1883 configs/standard3.yaml
```

On a machine without camera, serial, CAN, or model assets, those programs may stop before full closed-loop runtime. Record the exact error instead of treating that as a pass.
