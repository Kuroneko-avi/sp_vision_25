# MQTT Dashboard Broker Runbook

本文档用于第一阶段 Dashboard MQTT 地基验证：本机 Mosquitto Broker 监听原生 MQTT `1883`，WebSocket MQTT `9001`，并为后续 C++ HTTP server 预留 `8080`。

## 端口规划

| 端口 | 用途 | 阶段一要求 |
| :--- | :--- | :--- |
| `1883/tcp` | 原生 MQTT | Mosquitto 必须监听 |
| `9001/tcp` | MQTT over WebSocket | Mosquitto 必须监听 |
| `8080/tcp` | C++ HTTP server | 仅预留，不由 Mosquitto 占用 |

若启用防火墙，需要放行 `1883/tcp`、`9001/tcp`、`8080/tcp`。Ubuntu `ufw` 示例：

```bash
sudo ufw allow 1883/tcp
sudo ufw allow 9001/tcp
sudo ufw allow 8080/tcp
sudo ufw status
```

## Ubuntu/Debian 安装

```bash
sudo apt update
sudo apt install -y mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

安装后可先确认工具存在：

```bash
command -v mosquitto
command -v mosquitto_pub
command -v mosquitto_sub
```

## 推荐 Mosquitto 配置

创建 `/etc/mosquitto/conf.d/dashboard.conf`：

```conf
listener 1883
allow_anonymous true

listener 9001
protocol websockets
allow_anonymous true
```

修改系统配置后重启 Broker：

```bash
sudo systemctl restart mosquitto
sudo systemctl status mosquitto --no-pager
```

## 本仓库检测脚本

只做检测，不会自动使用 `sudo` 修改系统文件：

```bash
bash scripts/dashboard_mqtt_check.sh
```

检测内容：

- `mosquitto`、`mosquitto_pub`、`mosquitto_sub` 是否存在。
- `1883`、`9001`、`8080` 的 TCP 监听状态。
- `/etc/mosquitto/conf.d/dashboard.conf` 是否存在，并包含 `listener 9001` 与 `protocol websockets`。

## 本仓库 smoke test

验证本机 `1883` 原生 MQTT 发布订阅链路：

```bash
bash scripts/dashboard_mqtt_smoke.sh
```

脚本会订阅测试 topic，发布一条 JSON 消息，并校验订阅端收到的内容。`9001` WebSocket MQTT 在第一阶段只检查端口和配置，不在脚本中模拟浏览器 MQTT 客户端。

## 当前环境未验证项

截至 2026-04-26，本机未安装或未暴露 `mosquitto`、`mosquitto_pub`、`mosquitto_sub`，也未发现 `/etc/mosquitto/conf.d/dashboard.conf`。因此当前只完成脚本语法和只读检测路径验证，未完成真实 `1883` publish/subscribe 或 `9001` WebSocket 连接验证。
