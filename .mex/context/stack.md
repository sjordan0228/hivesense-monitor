---
name: stack
description: Technology stack, library choices, and the reasoning behind them. Load when working with specific technologies or making decisions about libraries and tools.
triggers:
  - "library"
  - "package"
  - "dependency"
  - "which tool"
  - "technology"
edges:
  - target: context/decisions.md
    condition: when the reasoning behind a tech choice is needed
  - target: context/conventions.md
    condition: when understanding how to use a technology in this codebase
  - target: context/architecture.md
    condition: when seeing how a tech choice fits into the overall system
last_updated: 2026-04-26
---

# Stack

## Core Technologies

- **C++17 / Arduino framework** — all firmware (hive-node, collector, sensor-tag, sensor-tag-wifi)
- **PlatformIO** — build system; `platform-espressif32` pinned to pioarduino fork `53.03.10` for ESP32-C6/S3 board support
- **ESP32 silicon** — ESP32-S3 (hive-node, S3-Zero variant), ESP32-C6 (sensor-tag, sensor-tag-wifi default), original ESP32-WROVER (collector via LilyGO T-SIM7080G)
- **Python 3.11+** (LXC) / 3.14 (local) — Django web app, deployment scripts, provisioning tooling
- **Django 5.2.13 LTS** — combsense-web app (Postgres + gunicorn + nginx)
- **InfluxDB 2.8 OSS + Flux** — time-series storage and queries
- **Telegraf** — MQTT → Influx ingest
- **Mosquitto** — local MQTT broker (Proxmox VM)

## Key Libraries

- **NimBLE-Arduino** (not the Arduino core's Bluedroid) — BLE stack for hive-node and sensor-tag. Saves 528 KB flash, required to fit dual OTA partitions.
- **PubSubClient** (knolleary/PubSubClient@^2.8) — MQTT client. Used everywhere; `flush()` before WiFi teardown is mandatory on C6 to avoid TX-drain races.
- **TinyGSM** — SIM7080G modem abstraction on the collector. TLS terminates on the modem, not the ESP32.
- **Adafruit SHT31 Library** (^2.2.2) — temp/humidity. Heater burns off condensation in hive humidity range.
- **OneWireNg** (^0.14.1) + **DallasTemperature** (^3.11.0) — DS18B20 path on sensor-tag-wifi (`xiao-c6-ds18b20`, `waveshare-s3zero-ds18b20` envs). The legacy `OneWire` library is `lib_ignore`d.
- **Unity** (PlatformIO native test framework) — host-machine unit tests. sensor-tag-wifi has 38 native tests across payload, OTA manifest parser, OTA decision, OTA validate-on-boot, sha256 streamer, battery math.
- **gunicorn** + **nginx 1.22** — Django serving (combsense-web LXC).

## What We Deliberately Do NOT Use

- **No Bluedroid** — NimBLE only. Bluedroid costs 528 KB flash that we need for dual OTA partitions.
- **No DHT22** — SHT31 only. DHT22 degrades within months in hive humidity; SHT31's heater + I2C + dual-address support is worth the $2/sensor premium.
- **No Docker on the LXC stack** — native Debian 12 packages on Proxmox NFS. Docker has known storage and networking footguns there.
- **No `esp_http_client` / `esp-tls` / `getaddrinfo` on ESP32-C6 for OTA** — that path routes through OpenThread DNS64 and fails (EAI_FAIL/202) for IPv4 literals. Use raw `WiFiClient` + `IPAddress::fromString` instead. Same `WiFiClient` is also reused by PubSubClient.
- **No measurement renames in Influx downsample tasks** — all three buckets keep `_measurement = "sensor_reading"`. The bucket signals granularity, not the measurement name. iOS Flux query depends on this.
- **No per-yard MQTT brokers yet** — single Mosquitto until cellular yards land. One subscribe pattern in Telegraf covers everything.

## Version Constraints

- **PlatformIO ESP32 platform pinned** to `https://github.com/pioarduino/platform-espressif32/releases/download/53.03.10/...` — required for ESP32-C6/S3 support, plus ARDUINO_USB_CDC_ON_BOOT=1 + ARDUINO_USB_MODE=1 USB-CDC behavior.
- **Django 5.2.13 LTS** (not 5.0.x) — fixes the Python 3.14 context-copy regression that hits us locally.
- **InfluxDB 2.8** (not 3.x) — Flux is required by the iOS HistoryService. Influx 3 dropped Flux.
- **Python 3.11 on LXC, 3.14 locally** — `web/.venv/` is local-only and not committed; the LXC has its own venv built from `requirements.txt`.
- **18650 + solar power budget** — sensor-tag-wifi assumes ~700 mAh/day harvest (5V × 200mA × 5h × 70%) and >45 mA average load avoidance during deep sleep (no MH-CD42-style auto-shutoff modules).
