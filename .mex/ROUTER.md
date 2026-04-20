---
name: router
description: Session bootstrap and navigation hub. Read at the start of every session before any task.
last_updated: 2026-04-20 (Django web scaffold landed; combsense-web Task 2 complete)
---

## Infrastructure

- **Mosquitto broker:** 192.168.1.82:1883 (user `hivesense`)
- **combsense-tsdb LXC:** 192.168.1.19 — Proxmox LXC 124, NFS-backed, Debian 12 (unprivileged)
  - **InfluxDB 2.8** on `:8086`, org `combsense`
    - Buckets: `combsense` (raw, 30d) / `combsense_1h` (hourly, 365d) / `combsense_1d` (daily, ∞)
    - Measurement: `sensor_reading`. Tag: `sensor_id`. Fields: `t1`, `t2`, `b`, `sensor_ts`.
    - Downsample tasks live in Influx metadata; canonical copies in `deploy/tsdb/downsample-*.flux`
  - **Grafana 13** on `:3000`
  - **Telegraf** — MQTT consumer `combsense/hive/+/reading` → Influx; config mirrored at `deploy/tsdb/telegraf-combsense.conf`
  - Tokens at `/root/.combsense-tsdb-creds` (mode 600): `admin_token`, `telegraf_write_token`, `ios_read_token`
  - Systemd sandboxing drop-ins at `/etc/systemd/system/{grafana-server,telegraf}.service.d/override.conf` (required for unprivileged LXC — see memory)
  - Daily Influx backup via `combsense-backup.timer` → `/var/backups/combsense-tsdb/`, 14-day retention
- **Remote:** only `origin` on GitHub (`sjordan0228/combsense-monitor`). Branches: `main` (prod), `dev` (integration).

# Session Bootstrap

Read this file fully before doing anything else in this session.

## Current Project State

**Phase: home-yard pipeline live (firmware → MQTT → Influx → iOS history graph). Apiary-side hive-node + collector await hardware.**

### Completed
- Hardware datasheet and design spec (README.md)
- Hive node firmware (`firmware/hive-node/`) — Freenove ESP32-S3 Lite (8MB flash)
  - State machine with power-aware sleep/wake cycle
  - SHT31 dual temp/humidity, HX711 weight, battery ADC
  - ESP-NOW with packet wrapping, TIME_SYNC receive, OTA routing
  - BLE GATT server (NimBLE) with sensor log sync and pairing
  - LittleFS circular buffer storage (500 readings)
  - OTA update receive module with CRC32 and auto-rollback
  - BLE tag reader for wireless internal sensor tag
  - Build: 27.7% flash (1.0 MB of 3.5 MB)
- Yard collector firmware (`firmware/collector/`) — LilyGO T-SIM7080G
  - ESP-NOW receiver with payload buffering and MAC tracking
  - Cellular module (TinyGSM, SIM7080G) with NTP sync
  - MQTT batch publisher to HiveMQ Cloud
  - OTA relay (download from GitHub, chunk to hive nodes via ESP-NOW)
  - Self-OTA (download and apply own firmware)
  - Time sync broadcast to hive nodes every publish cycle
  - Build: 24.5% flash (900 KB of 3.5 MB)
- Shared protocol headers (`firmware/shared/`)
- Wireless sensor tag firmware (`firmware/sensor-tag/`) — XIAO ESP32-C6
  - BLE advertisement with temp/humidity from SHT31
  - Deep sleep cycle (60s interval), CR2032 powered
  - Build: 38% flash (1.2 MB of 3.0 MB)
- Sensor tag WiFi variant (`firmware/sensor-tag-wifi/`) — XIAO ESP32-C6 for home-yard deployments
  - Compile-time sensor abstraction (SHT31 dual / DS18B20 dual)
  - Direct MQTT to local Mosquitto, RTC ring buffer for offline resilience
  - BSSID caching in RTC for fast reconnect
  - 18650 + solar powered, 5-min sample cadence by default
  - Native Unity tests for payload serialization (6 passing, incl. t=0 case)
  - Epoch timestamps via NTP sync in `drainBuffer()` — persists across deep sleep via RTC; pre-sync readings emit `t=0` which Telegraf replaces with arrival time
  - NaN temperatures serialize as JSON `null` (not `nan`) so Telegraf/Swift/Postgres parsers accept them
  - USB-CDC serial console provisioning (WiFi/MQTT creds via `tools/provision_tag.py`)
- **TSDB stack** (`combsense-tsdb` LXC, `deploy/tsdb/` for canonical configs)
  - Telegraf MQTT → Influx pipeline, arrival-time stamped, firmware `t` preserved as `sensor_ts` field
  - Downsample tasks: 15m cadence into `combsense_1h`, 6h cadence from `_1h` into `combsense_1d`
  - Daily `influx backup` via systemd timer, 14-day retention on disk
- **iOS history feature** (in `sjordan0228/combsense`)
  - `HistoryService` — Flux query client with auto-bucket resolution (raw → 1h → 1d based on range) and CSV parser that accepts both fractional and whole-second RFC3339 timestamps
  - `HiveHistoryView` — Swift Charts view with 24h/7d/30d/1y range picker, reached via NavigationLink from hive detail
  - Settings pane extended with Influx URL + org (AppStorage) and read token (Keychain)

- **combsense-web Django scaffold** (`web/`) — Task 2 done
  - `web/combsense/` project package: env-driven `settings.py`, stock `urls.py`/`wsgi.py`/`asgi.py`
  - `web/accounts/` skeleton (empty `__init__.py`, `apps.py`, `models.py` stub — Task 3 adds full User model)
  - `web/core/` skeleton (empty `__init__.py`, `apps.py`)
  - `web/requirements.txt`, `web/.env.example`, `web/pytest.ini`, `web/conftest.py`
  - `web/.venv/` (Python 3.14, not committed); `web/.env` (not committed)
  - `manage.py check` passes clean

### Not yet built
- combsense-web Task 3: custom User model + TDD (accounts app)
- combsense-web Tasks 4–N: views, URLs, templates, deployment
- Phase 2: IR bee counter (8-pair beam-break array via CD74HC4067 mux)
- CombSense iOS app BLE/MQTT live-reading integration (separate from history)
- 3D printed enclosures and sensor gate
- HiveMQ Cloud account setup (local Mosquitto covers home yard; cellular remote still ahead)

**Related repos:**
- `sjordan0228/combsense` — iOS app (SwiftUI + SwiftData)
- `sjordan0228/combsense-monitor` — this repo (hardware + firmware + TSDB configs)

## Routing Table

| Task type | Load |
|-----------|------|
| Understanding the hardware design | `README.md` (the full datasheet) |
| Working on ESP32 firmware | `firmware/hive-node/` directory |
| Working on collector firmware | `firmware/collector/` directory |
| Working on home-yard WiFi variant | `firmware/sensor-tag-wifi/` directory |
| Shared firmware headers | `firmware/shared/` directory |
| Making a design decision | `.mex/context/decisions.md` |
| Writing or reviewing code | `.mex/context/conventions.md` |
| TSDB / Influx / Telegraf / downsampling | `deploy/tsdb/` + Infrastructure section above |
| iOS history feature | `sjordan0228/combsense` repo (separate session) |
| Django web dashboard (combsense-web) | `web/` directory |

## Behavioural Contract

For every task, follow this loop:

1. **CONTEXT** — Load the relevant context file(s) from the routing table above.
2. **BUILD** — Do the work on `dev` (not `main`). Feature branches off `dev`.
3. **VERIFY** — Build and test.
4. **GROW** — Update context files (this file, `decisions.md`, `conventions.md`) when anything architectural or operational changes. Not just ROUTER — the underlying files too.
