FROM python:3.12-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends bash mosquitto ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY docker/dashboard/mosquitto.conf /etc/mosquitto/mosquitto.conf
COPY docker/dashboard/entrypoint.sh /usr/local/bin/dashboard-entrypoint
COPY dashboard/index.html /app/dashboard/index.html

RUN chmod +x /usr/local/bin/dashboard-entrypoint

EXPOSE 1883 9001 8080

ENTRYPOINT ["dashboard-entrypoint"]
