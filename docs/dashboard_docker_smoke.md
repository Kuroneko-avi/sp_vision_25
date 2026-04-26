# Dashboard Docker Smoke

本文档用于第二阶段任务 E：在不运行真实 C++ 视觉程序的情况下，用单容器验证 MQTT native、MQTT over WebSocket、HTTP Dashboard 与 mock telemetry 链路。

## 启动

默认 `ROBOT_ID` 为 `myrobot`：

```bash
docker compose -f docker-compose.dashboard.yml up --build
```

覆盖 `ROBOT_ID`：

```bash
ROBOT_ID=hero docker compose -f docker-compose.dashboard.yml up --build
```

容器内会同时启动：

- Mosquitto native MQTT：`1883`
- Mosquitto MQTT over WebSocket：`9001`
- 静态 HTTP server：`8080`
- mock telemetry publisher：每秒向 `{ROBOT_ID}/data` 发布 QoS 0 JSON

mock payload 使用正式协议字段 `values`：

```json
{"timestamp":1770000000000,"values":{"speed":2.5,"temp":42.0}}
```

## 访问 Dashboard

在浏览器打开：

```text
http://主机IP:8080
```

本机访问可用：

```text
http://127.0.0.1:8080
```

Dashboard 页面中：

- `Broker Host` 填主机 IP 或 `127.0.0.1`。
- `Robot ID` 默认填 `myrobot`，若启动时覆盖了 `ROBOT_ID`，这里也填同一个值。
- 点击 `Connect` 后，前端会通过 `ws://<Broker Host>:9001` 订阅 `{ROBOT_ID}/data`。

## 确认端口

宿主机检查端口：

```bash
ss -ltn '( sport = :1883 or sport = :9001 or sport = :8080 )'
```

HTTP 检查：

```bash
curl -I http://127.0.0.1:8080
```

Compose 配置检查：

```bash
docker compose -f docker-compose.dashboard.yml config
```

## 查看 mock telemetry 日志

前台启动时日志会直接输出。后台启动时可查看：

```bash
docker compose -f docker-compose.dashboard.yml logs -f dashboard-smoke
```

日志中应出现类似：

```text
[mock_publisher] myrobot/data {"timestamp":...,"values":{"speed":...,"temp":...}}
```

## 停止

前台运行时按 `Ctrl-C`。后台运行时执行：

```bash
docker compose -f docker-compose.dashboard.yml down
```

`entrypoint.sh` 会在收到 `SIGTERM` / `SIGINT` 后终止 Mosquitto、HTTP server 与 mock publisher，并等待子进程退出。

## 当前环境未验证项

截至 2026-04-26，如果本机缺少 Docker 或网络无法拉取镜像/安装 Python 依赖，则只能完成静态检查，不能完成真实容器 smoke test。

若本机缺少 `mosquitto_pub` / `mosquitto_sub`，不影响容器内 mock telemetry，但无法在宿主机直接用 mosquitto clients 订阅验证 `1883`。
