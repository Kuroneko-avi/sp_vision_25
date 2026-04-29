# MQTT Dashboard Broker Runbook

本文档用于 Dashboard MQTT 网络服务验证：Dashboard 容器提供原生 MQTT `1883`、WebSocket MQTT `9001` 和静态 HTTP Dashboard `8080`。

## 端口规划

| 端口 | 用途 | 当前要求 |
| :--- | :--- | :--- |
| `1883/tcp` | 原生 MQTT | Mosquitto 必须监听 |
| `9001/tcp` | MQTT over WebSocket | Mosquitto 必须监听 |
| `8080/tcp` | HTTP Dashboard | Python static server 必须监听 |

`docker-compose.dashboard.yml` 默认发布 `1883/9001/8080` 到主机全部网卡，允许局域网内其他主机访问。

- 浏览器访问：`http://主机IP:8080`
- 前端 Broker URL：`ws://主机IP:9001`
- 主程序同机连接：`tcp://127.0.0.1:1883`
- 主程序在另一台机器或另一网络命名空间中连接：`tcp://主机IP:1883`

如果未来只想本机访问，可以手动把 compose 端口改为 `127.0.0.1:端口:端口`，但本次默认不要这样做。

若启用防火墙，需要放行 `1883/tcp`、`9001/tcp`、`8080/tcp`。Ubuntu `ufw` 示例：

```bash
sudo ufw allow 1883/tcp
sudo ufw allow 9001/tcp
sudo ufw allow 8080/tcp
sudo ufw status
```

## 可选：Ubuntu/Debian 系统级 Mosquitto 安装

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

## 可选：系统级 Mosquitto 配置

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

- `1883`、`9001`、`8080` 的 TCP 监听状态。
- `http://127.0.0.1:8080` 是否响应。
- `mosquitto`、`mosquitto_pub`、`mosquitto_sub` 是否存在；缺失只输出 WARN。
- `/etc/mosquitto/conf.d/dashboard.conf` 是否存在；仅作为系统级 Mosquitto 可选检查，缺失不导致失败。

脚本退出码只由 Docker Dashboard 网络服务端口、HTTP 响应或必要检测工具缺失决定；缺少 mosquitto clients 或系统配置文件不会导致失败。

## 本仓库 smoke test

验证本机 `1883` 原生 MQTT 发布订阅链路：

```bash
bash scripts/dashboard_mqtt_smoke.sh
```

脚本会订阅测试 topic，发布一条 JSON 消息，并校验订阅端收到的内容。`9001` WebSocket MQTT 在第一阶段只检查端口和配置，不在脚本中模拟浏览器 MQTT 客户端。

## 生产边界

当前只有两个服务形态：Dashboard 网络服务容器，以及单独启动的真实视觉主程序。不包含 image/video/MJPEG、Web terminal、mock runtime、video-source、hardwareless smoke 或 mock publisher。
