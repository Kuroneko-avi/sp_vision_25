# Dashboard Frontend Split

领导要求 `sp_vision_25` 长期保持为视觉代码仓库，因此 Dashboard panel 前端已从本仓库拆出。当前仓库只保留 MQTT 后端能力：topic/payload 契约、C++ bridge、参数 schema/current 和 `auto_aim_debug_mpc` 接入。

## Target Repository

建议新建独立仓库：

```text
sp_vision_dashboard_panel
```

建议职责：

- 提供 Dashboard browser UI。
- 提供 style assets 和 browser-side modules。
- 提供 Mosquitto native MQTT `1883`。
- 提供 MQTT over WebSocket `9001`。
- 提供 HTTP Dashboard `8080`。
- 不包含视觉算法代码。
- 只通过 MQTT 协议和 `sp_vision_25` 通信。

## Suggested Layout

为避免视觉仓库重新出现前端残留，这里用语义化名称描述迁移结构；新仓库可按前端团队习惯命名入口 HTML、样式目录、脚本目录和第三方依赖目录。

```text
sp_vision_dashboard_panel/
  dashboard/
    html entry
    styles/
    browser modules/
    third party frontend assets/
  docker/
    Dockerfile
    mosquitto.conf
    entrypoint.sh
  docker-compose.yml
  docs/
    usage.md
    protocol.md
  README.md
```

## Seed Copy

本次提交前已把当前前端成果复制到未纳入 git 的临时目录：

```text
/tmp/sp_vision_dashboard_panel_seed/
```

该目录包含：

- 旧浏览器 Dashboard 资源目录。
- 旧 Dashboard service container 目录。
- 旧 compose 文件副本。
- MQTT 协议文档副本。

该目录不属于 `sp_vision_25`，不会随本仓库提交。创建新仓库时，可从该临时目录迁入需要保留的前端成果。

## Contract Source

协议仍以本仓库为准：

```text
docs/dashboard_mqtt_protocol.md
tools/dashboard_mqtt_contract.hpp
```

前端仓库应复制或引用协议文档，但不能绕过该契约直接改变 topic、payload、QoS 或控制语义。

## Deployment Reminder

同机部署：

```text
auto_aim_debug_mpc -> tcp://127.0.0.1:1883
browser device -> http://robot-lan-ip:8080
browser MQTT WS -> ws://robot-lan-ip:9001
```

分离部署：

```text
auto_aim_debug_mpc -> tcp://debug-pc-lan-ip:1883
browser device -> http://debug-pc-lan-ip:8080
browser MQTT WS -> ws://debug-pc-lan-ip:9001
```
