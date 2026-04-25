---
name: router
description: Session bootstrap and navigation hub. Read at the start of every session before any task.
last_updated: 2026-04-25 (sensor-tag-wifi HTTP-pull OTA happy-path validated end-to-end on c5fffe12; tag deployed to yard on 5423c04; Battery::percentFromMillivolts extracted inline + 6 native tests added on ceb0fa4)
---

## Infrastructure

- **Mosquitto broker:** 192.168.1.82:1883 (user `hivesense`)
- **combsense-tsdb LXC:** 192.168.1.19 — Proxmox LXC 124, NFS-backed, Debian 12 (unprivileged)
  - **InfluxDB 2.8** on `:8086`, org `combsense`
    - Buckets: `combsense` (raw, 30d) / `combsense_1h` (hourly, 365d) / `combsense_1d` (daily, ∞)
    - Measurement: `sensor_reading`. Tag: `sensor_id`. Fields: `t1`, `t2`, `b`, `sensor_ts`.
    - Downsample tasks live in Influx metadata; canonical copies in `deploy/tsdb/downsample-*.flux`
  - **Grafana 13** on `:3000` — `combsense-home-yard` dashboard provisioned (temp °F, battery, last-seen); JSON canonical at `deploy/tsdb/grafana/home-yard-sensors.json`
  - **Telegraf** — MQTT consumer `combsense/hive/+/reading` → Influx; config mirrored at `deploy/tsdb/telegraf-combsense.conf`
  - Tokens at `/root/.combsense-tsdb-creds` (mode 600): `admin_token`, `telegraf_write_token`, `ios_read_token`
  - Systemd sandboxing drop-ins at `/etc/systemd/system/{grafana-server,telegraf}.service.d/override.conf` (required for unprivileged LXC — see memory)
  - Daily Influx backup via `combsense-backup.timer` → `/var/backups/combsense-tsdb/`, 14-day retention
- **combsense-web LXC:** 192.168.1.61 — Proxmox LXC 125, NFS-backed, Debian 12 (unprivileged)
  - **PostgreSQL 15** on 127.0.0.1:5432 (db: `combsense`, user: `combsense`)
  - **Redis 7** on 127.0.0.1:6379 (for Celery later)
  - **Django (gunicorn)** on 127.0.0.1:8000 via `combsense-web.service`
  - **nginx 1.22** on :80 / :443 — reverse-proxies gunicorn, serves `/static/` directly, self-signed TLS for `dashboard.combsense.com` (cert at `/etc/ssl/combsense/`)
  - Credentials at `/root/.combsense-web-creds` (mode 600)
  - Systemd drop-in at `/etc/systemd/system/combsense-web.service.d/override.conf` (unprivileged LXC workaround)
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
  - Native Unity tests: 36 passing across payload (6), OTA manifest parser (9), OTA decision (6), OTA validate-on-boot (4), sha256 streamer (5), battery math (6)
  - Epoch timestamps via NTP sync in `drainBuffer()` — persists across deep sleep via RTC; pre-sync readings emit `t=0` which Telegraf replaces with arrival time
  - NaN temperatures serialize as JSON `null` (not `nan`) so Telegraf/Swift/Postgres parsers accept them
  - USB-CDC serial console provisioning (WiFi/MQTT/OTA creds via `tools/provision_tag.py --ota-host ...`)
  - HTTP-pull OTA on wake (manifest at `http://192.168.1.61/firmware/sensor-tag-wifi/<variant>/manifest.json`, sha256-verified, dual 1.5 MB OTA slots, bootloader auto-rollback if first publish after flash fails). Publish via `deploy/web/publish-firmware.sh <sht31|ds18b20|s3-ds18b20>`. nginx LAN-only allowlist on combsense-web LXC.
  - **Hardware variants:** Seeed XIAO ESP32-C6 (default; envs `xiao-c6-sht31`, `xiao-c6-ds18b20`) and Waveshare ESP32-S3-Zero (env `waveshare-s3zero-ds18b20`, OTA variant `s3-ds18b20`). Pin map differs (S3: OneWire→GPIO4, batt ADC→GPIO1) via build-flag overrides in `include/config.h`. S3 board flagged as `esp32-s3-devkitc-1` because `waveshare_esp32_s3_zero` is not in the pioarduino board index. C6 yard tag unaffected by S3 work.
  - **OTA transport:** raw `WiFiClient` + `IPAddress::fromString` for OTA fetches — bypasses `esp_http_client` / `esp-tls` / `getaddrinfo`, which on the C6 routes through OpenThread DNS64 and fails (EAI_FAIL/202) for IPv4 literals. PubSubClient uses the same WiFiClient transport. WiFi window is held across upload + OTA (was: connect → publish → disconnect → check; now: connect → publish → check → disconnect).
  - **Validated 2026-04-25:** tag c5fffe12 OTA'd a720183 → 5423c04 end-to-end (download 1,056,480 B, sha256 verified, reboot, new fw published over MQTT, sleep 300s). Tag deployed to yard. Tasks 15b–15e (sha-mismatch / auto-rollback / low-battery skip / failed-version pin) still need bench validation.
- **TSDB stack** (`combsense-tsdb` LXC, `deploy/tsdb/` for canonical configs)
  - Telegraf MQTT → Influx pipeline, arrival-time stamped, firmware `t` preserved as `sensor_ts` field
  - Downsample tasks: 15m cadence into `combsense_1h`, 6h cadence from `_1h` into `combsense_1d`
  - Daily `influx backup` via systemd timer, 14-day retention on disk
- **iOS history feature** (in `sjordan0228/combsense`)
  - `HistoryService` — Flux query client with auto-bucket resolution (raw → 1h → 1d based on range) and CSV parser that accepts both fractional and whole-second RFC3339 timestamps
  - `HiveHistoryView` — Swift Charts view with 24h/7d/30d/1y range picker, reached via NavigationLink from hive detail
  - Settings pane extended with Influx URL + org (AppStorage) and read token (Keychain)

- **combsense-web Plan D live** (`web/`, `deploy/web/`, `combsense-web` LXC, nginx TLS reverse proxy on 192.168.1.61)
  - nginx 1.22 terminates TLS on :443 with self-signed cert at `/etc/ssl/combsense/`
  - :80 redirects to HTTPS; ACME challenge passthrough at `/.well-known/acme-challenge/`
  - gunicorn on 127.0.0.1:8000 behind nginx proxy; static files served directly by nginx
  - env has `DJANGO_SECURE_COOKIES=1`, `DJANGO_CSRF_TRUSTED_ORIGINS`, `DJANGO_DEBUG=0`
  - provision.sh requires `BRANCH=dev` until Plan D merges to main
  - Phase 2: swap to Let's Encrypt cert via certbot, uncomment HSTS header
- **combsense-web Plan A complete** (`web/`, `deploy/web/`, `combsense-web` LXC)
  - `web/combsense/` project package: env-driven `settings.py`, `urls.py` routes admin + accounts + core
  - `web/accounts/`: custom User model (email login, `role` field), `EmailAuthenticationForm`, `CombSenseLoginView`, `CombSenseLogoutView` (POST-only), `accounts:login` / `accounts:logout` / 4 password-reset URLs namespaced
  - `EMAIL_BACKEND` reads from `DJANGO_EMAIL_BACKEND` env (console fallback for dev; Plan D wires SMTP); warning comment on silent prod failure mode
  - `web/core/`: `core:home` template-rendered view (login_required) with logout form + conditional admin link
  - `web/templates/`: `base.html` shell, `registration/login.html`, password-reset templates; `core/templates/core/home.html`
  - `web/requirements.txt` (Django 5.2.13 LTS — upgraded from 5.0.9 to fix Python 3.14 context copy regression)
  - **Tests:** 19 passing across `accounts` and `core` (8 model, 5 auth view, 3 password reset, 3 home view)
  - **Deploy artifacts** (`deploy/web/`): `combsense-web.service` (gunicorn systemd unit, `WorkingDirectory=/opt/combsense-web/web`), `combsense-web.service.d/override.conf` (unprivileged LXC sandboxing workaround), `env.template`, `provision.sh` (idempotent bootstrap via `env --file`; runs migrate/collectstatic from `${INSTALL_DIR}/web`), `README.md` (operator runbook)
  - `web/.venv/` (Python 3.14 locally; LXC runs Python 3.11; not committed); `web/.env` (not committed)

### Not yet built
- combsense-web Plan B onward: MQTT ingest watcher (auto-claim devices), hive list/detail, Influx reader, Chart.js rendering, alerts, OTA dispatch
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
| Sensor-tag-wifi OTA | `firmware/sensor-tag-wifi/src/ota*.cpp` + `deploy/web/publish-firmware.sh` |
| Sensor-tag-wifi pin map / variants | `firmware/sensor-tag-wifi/include/config.h` + `platformio.ini` build_flags |
| Shared firmware headers | `firmware/shared/` directory |
| Making a design decision | `.mex/context/decisions.md` |
| Writing or reviewing code | `.mex/context/conventions.md` |
| TSDB / Influx / Telegraf / downsampling | `deploy/tsdb/` + Infrastructure section above |
| iOS history feature | `sjordan0228/combsense` repo (separate session) |
| Django web dashboard (combsense-web) | `web/` directory |
| combsense-web LXC ops | deploy/web/README.md |

## Behavioural Contract

For every task, follow this loop:

1. **CONTEXT** — Load the relevant context file(s) from the routing table above.
2. **BUILD** — Do the work on `dev` (not `main`). Feature branches off `dev`.
3. **VERIFY** — Build and test.
4. **GROW** — Update context files (this file, `decisions.md`, `conventions.md`) when anything architectural or operational changes. Not just ROUTER — the underlying files too.
