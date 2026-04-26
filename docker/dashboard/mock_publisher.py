#!/usr/bin/env python3
import json
import math
import os
import random
import signal
import sys
import time

import paho.mqtt.client as mqtt


BROKER_HOST = os.environ.get("MQTT_HOST", "127.0.0.1")
BROKER_PORT = int(os.environ.get("MQTT_PORT", "1883"))
ROBOT_ID = os.environ.get("ROBOT_ID", "myrobot")
TOPIC = f"{ROBOT_ID}/data"
INTERVAL_SECONDS = float(os.environ.get("MOCK_INTERVAL_SECONDS", "1.0"))

running = True


def handle_signal(signum, frame):
    del signum, frame
    global running
    running = False


def epoch_ms():
    return int(time.time() * 1000)


def build_payload(step):
    speed = 2.5 + math.sin(step / 5.0) * 1.2 + random.uniform(-0.08, 0.08)
    temp = 42.0 + math.sin(step / 11.0) * 3.0 + random.uniform(-0.2, 0.2)
    return {
        "timestamp": epoch_ms(),
        "values": {
            "speed": round(speed, 3),
            "temp": round(temp, 3),
        },
    }


def connect_with_retry(client):
    while running:
        try:
            client.connect(BROKER_HOST, BROKER_PORT, keepalive=30)
            return True
        except OSError as error:
            print(
                f"[mock_publisher] waiting for MQTT broker {BROKER_HOST}:{BROKER_PORT}: {error}",
                flush=True,
            )
            time.sleep(1)
    return False


def main():
    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    if not connect_with_retry(client):
        return 0

    client.loop_start()
    print(f"[mock_publisher] publishing QoS 0 telemetry to {TOPIC}", flush=True)

    step = 0
    try:
        while running:
            payload = build_payload(step)
            message = json.dumps(payload, separators=(",", ":"))
            result = client.publish(TOPIC, message, qos=0)
            if result.rc != mqtt.MQTT_ERR_SUCCESS:
                print(f"[mock_publisher] publish failed rc={result.rc}", flush=True)
                return 1
            print(f"[mock_publisher] {TOPIC} {message}", flush=True)
            step += 1
            time.sleep(INTERVAL_SECONDS)
    finally:
        client.loop_stop()
        client.disconnect()

    return 0


if __name__ == "__main__":
    sys.exit(main())
