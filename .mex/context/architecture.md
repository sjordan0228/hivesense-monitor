---
name: architecture
description: How the major pieces of this project connect and flow. Load when working on system design, integrations, or understanding how components interact.
triggers:
  - "architecture"
  - "system design"
  - "how does X connect to Y"
  - "integration"
  - "flow"
edges:
  - target: context/stack.md
    condition: when specific technology details are needed
  - target: context/decisions.md
    condition: when understanding why the architecture is structured this way
  - target: context/setup.md
    condition: when bringing up a new component or LXC service
last_updated: 2026-04-26
---

# Architecture

## System Overview

Two parallel ingestion paths feed the same observability layer.

**Remote-yard path (cellular):** hive nodes (ESP32-S3) sample sensors → broadcast `HivePayload` over ESP-NOW (250-byte frames) → yard collector (LilyGO T-SIM7080G) buffers + batch-publishes via cellular MQTT to HiveMQ Cloud → iOS app subscribes for live; Telegraf relays to Influx for history.

**Home-yard path (WiFi):** sensor-tag-wifi nodes (XIAO ESP32-C6) connect WiFi on each wake → publish JSON to local Mosquitto (`192.168.1.82`) → Telegraf consumes `combsense/hive/+/reading` → InfluxDB 2.8 (combsense-tsdb LXC) → Grafana dashboards + iOS history (Flux query). Direct path; no collector. Offline samples buffered in RTC ring (24-byte `Reading` × N).

**Direct-at-yard path (BLE):** hive nodes expose a NimBLE GATT server with full sensor-log sync — iOS app connects when within ~30 ft, no internet required.

**OTA dispatch:** sensor-tag-wifi pulls firmware from `combsense-web` nginx (HTTP, sha256-verified, dual 1.5 MB OTA slots, bootloader auto-rollback). Hive nodes get OTA via collector relay over ESP-NOW.

## Key Components

- **Hive Node firmware** (`firmware/hive-node/`) — Freenove ESP32-S3 Lite (8 MB flash). Power-aware state machine, SHT31 + HX711 + ADC, ESP-NOW publisher, NimBLE GATT, LittleFS circular log (500 readings), OTA receiver with CRC32 + auto-rollback.
- **Yard Collector firmware** (`firmware/collector/`) — LilyGO T-SIM7080G. ESP-NOW receiver with payload buffering + MAC tracking, TinyGSM cellular, MQTT batch publisher (30-min cycle), OTA relay (GitHub → ESP-NOW chunked), TIME_SYNC broadcast every publish.
- **Sensor-tag-wifi firmware** (`firmware/sensor-tag-wifi/`) — XIAO ESP32-C6 (default) or Waveshare ESP32-S3-Zero (NOT recommended for solar/sleep). Compile-time sensor abstraction (SHT31 dual / DS18B20 dual), direct MQTT to local Mosquitto, RTC ring buffer for offline resilience, BSSID caching, HTTP-pull OTA, USB-CDC console provisioning.
- **Sensor-tag firmware (BLE)** (`firmware/sensor-tag/`) — XIAO ESP32-C6, CR2032-powered BLE advertiser. Read by hive node tag-reader.
- **Shared headers** (`firmware/shared/`) — `HivePayload` struct, ESP-NOW packet wrapping, OTA protocol constants. Both hive-node and collector include these.
- **TSDB stack** (`deploy/tsdb/`, `combsense-tsdb` LXC at 192.168.1.19) — Telegraf MQTT→Influx, InfluxDB 2.8 with three-bucket retention (raw 30d / 1h 365d / 1d ∞), Grafana 13 with provisioned dashboards.
- **combsense-web** (`web/`, `deploy/web/`, `combsense-web` LXC at 192.168.1.61) — Django 5.2 LTS (gunicorn + nginx TLS), Postgres 15, Redis 7 (for Celery later). Hosts firmware OTA endpoint (`/firmware/sensor-tag-wifi/<variant>/`).

## External Dependencies

- **HiveMQ Cloud** (free tier, planned for cellular yards) — remote-yard MQTT broker reached over the collector's cellular link. Local Mosquitto covers home-yard until the first cellular site lands.
- **Mosquitto** (Proxmox VM at 192.168.1.82, user `hivesense`) — home-yard MQTT broker. Single broker pattern; no per-yard brokers until cellular yards justify the split.
- **InfluxDB 2.8 OSS** (combsense-tsdb LXC) — time-series store. iOS reads via Flux `/api/v2/query` directly — no server-side shim. Three-token auth: admin / telegraf-write / ios-read.
- **Telegraf** (combsense-tsdb LXC) — MQTT consumer subscribed to `combsense/hive/+/reading`. Arrival-time stamping (firmware `t=0` pre-NTP would otherwise reject); firmware `t` preserved as `sensor_ts` field for forensic alignment.
- **GitHub Actions** (`.github/workflows/`) — CI builds for all four firmware envs + native unit tests.
- **Ollama on 192.168.1.16** (qwen3-coder:30b) — pre-PR code review (mandatory) and boilerplate generation (saves Claude tokens).

## What Does NOT Exist Here

- **iOS app code** — separate repo `sjordan0228/combsense` (SwiftUI + SwiftData). This repo only contains the firmware/backend that the app talks to.
- **3D enclosure CAD files** — planned in README BOM but not yet started.
- **IR bee counter firmware** — Phase 2; the 8-pair beam-break array and CD74HC4067 mux work isn't built yet.
- **HiveMQ Cloud account / credentials** — local Mosquitto covers all current deployments. Will be set up when the first cellular yard goes live.
- **Per-service Docker** — the LXC stack is deliberately native Debian 12 + systemd. Docker on Proxmox NFS has known storage and networking footguns.
