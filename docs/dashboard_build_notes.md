# Dashboard Build Notes

本文档记录 Dashboard MQTT 构建依赖与 `MqttBridge` 接入范围。当前阶段实现 C++ MQTT bridge，但仍保持 Paho MQTT C++ 为可选依赖。

## CMake 接入范围

根 `CMakeLists.txt` include `cmake/DashboardDeps.cmake`。该文件执行两类 optional probe：

- `find_package(PahoMqttCpp QUIET)`：探测后续 C++ MQTT bridge 可用性。
- `find_path(DASHBOARD_CPP_HTTPLIB_INCLUDE_DIR NAMES httplib.h ...)`：探测 header-only `cpp-httplib` 是否已经可见。

如果 Paho MQTT C++ 不存在，configure 只输出 `STATUS` 提示，不会因为缺失该依赖而失败。`tools/mqtt_bridge.cpp` 与 `tests/mqtt_bridge_smoke.cpp` 仅在 `PahoMqttCpp_FOUND` 时加入构建。

## MqttBridge target

有 Paho MQTT C++ 时，`tools/CMakeLists.txt` 创建独立静态库：

```text
mqtt_bridge
```

该 target 链接 `nlohmann_json::nlohmann_json` 与 Paho MQTT C++，不并入既有 `tools` object library，避免 Paho 缺失时影响原有视觉程序。根 `CMakeLists.txt` 仅在 `TARGET mqtt_bridge` 存在时创建：

```text
mqtt_bridge_smoke
```

smoke 程序默认连接 `tcp://localhost:1883`，可通过环境变量覆盖：

```bash
MQTT_SERVER_URI=tcp://localhost:1883 MQTT_ROBOT_ID=hero ./build/mqtt_bridge_smoke
```

`MqttBridge` 只发布 `{robot_id}/data`、`{robot_id}/log`、`{robot_id}/params/schema`、`{robot_id}/params/current`、`{robot_id}/control/ack`，只订阅 `{robot_id}/control/param` 和 `{robot_id}/control/cmd`。当前阶段不实现图像、视频、MJPEG 或 `{robot_id}/image`。

I 阶段接入热参数时不要拆开 `DashboardParams` 生成的 envelope。直接调用：

```cpp
bridge.publish_params_schema_payload(dashboard_params.make_schema());
bridge.publish_params_current_payload(dashboard_params.make_current(timestamp));
```

`publish_params_schema()` 与 `publish_params_current()` 仍保留给只传 `params` 数组或 `values` 对象的旧式调用；完整 `params/schema`、`params/current` payload 应使用带 `_payload` 后缀的接口。

## 推荐安装方式

### Paho MQTT C++

可优先使用系统包安装；若发行版包名或版本不满足后续需求，再使用源码安装。

Ubuntu/Debian 包安装示例：

```bash
sudo apt update
sudo apt install -y libpaho-mqtt-dev libpaho-mqttpp-dev
```

源码安装建议以后续实际部署环境为准，确保安装后 CMake 可以通过 `find_package(PahoMqttCpp QUIET)` 找到 package config。

### cpp-httplib

`cpp-httplib` 是 header-only 库。推荐后续阶段将单头文件 vendor 到：

```text
third_party/httplib.h
```

当前阶段不强制该文件存在；`DashboardDeps.cmake` 只会尝试查找 `httplib.h` 并输出提示。

## 验证命令

```bash
cmake -B build
```

验收重点是 configure 不能因为 Paho MQTT C++ 或 `cpp-httplib` 缺失新增失败；若 Paho 缺失，`mqtt_bridge` 与 `mqtt_bridge_smoke` 应自动跳过。

## 当前环境未验证项

截至 2026-04-26，本机收尾检查只验证了脚本语法和前端内联 JavaScript 语法，未完成以下系统级验证：

- `cmake` 当前不在 `PATH` 中，因此未运行 `cmake -B build -DCMAKE_BUILD_TYPE=Release`。
- `nlohmann/json.hpp` 未在 `/usr/include/nlohmann/json.hpp` 发现，因此未编译验证 `tools/dashboard_mqtt_contract.hpp`。
- `shellcheck` 当前不在 `PATH` 中，因此脚本只运行 `bash -n`。
- `mosquitto`、`mosquitto_pub`、`mosquitto_sub` 当前不在 `PATH` 中，因此未做真实 Broker smoke test。
- Docker/Mosquitto 实例、C++ HTTP serve 与业务入口接入仍留到后续任务。
