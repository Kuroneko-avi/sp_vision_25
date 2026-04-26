# Dashboard Build Notes

本文档记录第一阶段 Dashboard 相关 CMake 依赖地基。当前阶段只做可选依赖探测，不实现 `MqttBridge`，不新增 executable，不链接任何现有 target。

## CMake 接入范围

根 `CMakeLists.txt` 只 include `cmake/DashboardDeps.cmake`。该文件当前只执行两类 optional probe：

- `find_package(PahoMqttCpp QUIET)`：探测后续 C++ MQTT bridge 可用性。
- `find_path(DASHBOARD_CPP_HTTPLIB_INCLUDE_DIR NAMES httplib.h ...)`：探测 header-only `cpp-httplib` 是否已经可见。

如果依赖不存在，configure 只输出 `STATUS` 提示，不会因为 Paho MQTT C++ 或 `cpp-httplib` 缺失而失败。当前阶段不创建 dashboard target，也不把探测结果链接到任何已有 target。

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

验收重点是 configure 不能因为 Paho MQTT C++ 或 `cpp-httplib` 缺失新增失败；若失败，应来自仓库原有的必需依赖检查或既有构建逻辑。

## 当前环境未验证项

截至 2026-04-26，本机收尾检查只验证了脚本语法和前端内联 JavaScript 语法，未完成以下系统级验证：

- `cmake` 当前不在 `PATH` 中，因此未运行 `cmake -B build -DCMAKE_BUILD_TYPE=Release`。
- `nlohmann/json.hpp` 未在 `/usr/include/nlohmann/json.hpp` 发现，因此未编译验证 `tools/dashboard_mqtt_contract.hpp`。
- `shellcheck` 当前不在 `PATH` 中，因此脚本只运行 `bash -n`。
- `mosquitto`、`mosquitto_pub`、`mosquitto_sub` 当前不在 `PATH` 中，因此未做真实 Broker smoke test。
- Docker/Mosquitto 实例、C++ HTTP serve、`MqttBridge` 与业务入口接入均留到第二阶段或后续任务。
