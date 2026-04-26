---
name: decisions
description: Architectural decisions and their rationale
triggers:
  - "decision"
  - "why did we"
  - "rationale"
  - "trade-off"
  - "ADR"
edges:
  - target: context/architecture.md
    condition: when a decision shaped the current system structure
  - target: context/stack.md
    condition: when a decision drove a specific technology choice
  - target: context/conventions.md
    condition: when a decision is enforced as a coding convention
last_updated: 2026-04-26
---

# Decisions

## ~~ESP32-WROOM-32 over ESP32-S3~~ → Switched to ESP32-S3
**Decision:** Use Freenove ESP32-S3 Lite (8MB flash) for hive nodes.
**Why:** 8MB flash enables dual OTA partitions. BLE 5.0 improves sync throughput. Native USB. WROOM-32E with 8MB was hard to source; the S3 Lite is readily available at ~$7.

## SHT31 over DHT22
**Decision:** Use Sensirion SHT31 for temperature and humidity.
**Why:** Built-in heater burns off condensation in 50-80% RH hive environment. DHT22 degrades within months inside a hive. SHT31 also has I2C (more reliable than DHT22's single-wire), dual address support (two sensors on same bus), and better accuracy (±0.3°C vs ±0.5°C). Extra $2/sensor is worth the reliability.

## ESP-NOW over WiFi/LoRa
**Decision:** Use ESP-NOW for hive node → collector communication.
**Why:** No router needed (apiaries have no WiFi). 200m range is enough for a yard. Connectionless — no pairing overhead. Built into ESP32 at no extra cost. Lower power than WiFi.

## HiveMQ Cloud over Home Assistant
**Decision:** Use HiveMQ Cloud (free tier) as MQTT broker instead of self-hosted Home Assistant.
**Why:** No server to maintain. Free tier covers 100 connections and 10GB/month — more than enough. iPhone app subscribes directly. Can always migrate to self-hosted Mosquitto ($5/mo VPS) if needed.

## BLE + MQTT dual path
**Decision:** Support both BLE direct (at the yard) and MQTT cloud (remote).
**Why:** Beekeepers need data at the yard without internet (BLE). They also want to check from home (MQTT). Both paths feed the same SwiftData model in the iOS app.

## IR beam-break over thermal/ToF for bee counting
**Decision:** Use IR break-beam pairs for v1 bee traffic counter.
**Why:** Simple, cheap ($2/pair), proven, weather-independent (unlike thermal which fails when ambient matches bee body temp). 4 directional lanes with entrance reducer gives good relative traffic data. Can upgrade to thermal/ToF later.

## ESP32-S3 over WROOM-32
**Decision:** Switch hive node target from ESP32-WROOM-32 (4MB) to Freenove ESP32-S3 Lite (8MB).
**Why:** 8MB flash enables dual OTA partitions with 2.5 MB headroom. BLE 5.0 improves sync throughput. Native USB eliminates UART chip for lower sleep current. ESP32-WROOM-32E with 8MB was hard to source; the S3 Lite is readily available at ~$7.

## NimBLE over Bluedroid
**Decision:** Replace ESP32 BLE Arduino (Bluedroid) with NimBLE-Arduino for BLE stack.
**Why:** Saves 528 KB flash — critical for fitting dual OTA partitions. API is nearly identical. Disabling unused roles (central, observer) saves additional flash. No functional downside for peripheral-only use.

## Collector-Relay OTA
**Decision:** OTA updates flow from iOS app → MQTT → collector → ESP-NOW → hive node.
**Why:** Hive nodes are remote with no internet. Collector has cellular. App-triggered (not automatic) gives explicit control over which nodes update and when. GitHub Releases hosts firmware binaries — free, versioned, existing workflow.

## Resumable Chunked OTA
**Decision:** OTA transfers use sequential 244-byte chunks over ESP-NOW with NVS progress tracking.
**Why:** ESP-NOW max payload is 250 bytes. Sequential ACK is simple and reliable. NVS progress survives sleep cycles. esp_ota_begin erases the partition so resume restarts from chunk 0, but full transfer completes in 1-2 wake cycles (~30-60 min) which is acceptable.

## TinyGSM for modem abstraction
**Decision:** Use TinyGSM library for SIM7080G modem control on the collector.
**Why:** Abstracts AT commands behind familiar Client interface. Supports SIM7080G's native MQTT stack — TLS terminates on the modem, not the ESP32. Well-tested with PlatformIO.

## Batch MQTT publishing
**Decision:** Collector buffers ESP-NOW payloads in RAM and publishes once per 30-min cycle.
**Why:** Modem is the biggest power draw. One modem-on per cycle (~30 sec) instead of per-node saves significant battery. iOS app already expects 30-min latency.

## Always-listening ESP-NOW (no scheduled windows)
**Decision:** Collector stays in light sleep with ESP-NOW radio active at all times.
**Why:** ESP32 internal RTC drifts 5-10 min/day. Scheduled listen windows would desync with hive nodes within days. NTP fixes collector drift but nodes have no internet. Time sync broadcast helps but adds fragility. Always-listening at ~5-8 mA is sustainable with the 5-10W solar panel.

## Time sync every publish cycle
**Decision:** Collector broadcasts TIME_SYNC via ESP-NOW after every MQTT publish, not once daily.
**Why:** Negligible cost (~100 µs per broadcast). Keeps hive node clocks accurate to within 30 minutes. Simpler logic — no "did I sync today" tracking.

## Multiplexer for IR array
**Decision:** Use 2× CD74HC4067 multiplexers for 8 IR pairs instead of direct GPIO.
**Why:** Saves 10 GPIO pins (20 → 10). Enables pulsed operation for power savings. $1/chip. Channels 8-15 available for future expansion to 16 pairs.

## InfluxDB 2.x over Timescale / plain Postgres
**Decision:** Use InfluxDB 2.8 OSS (native Debian 12 LXC, no Docker) as the history TSDB, with Telegraf as the ingest side and Grafana for house dashboards.
**Why:** First-class time-series semantics (buckets with per-bucket retention, task-scheduled Flux downsampling, HTTP `/api/v2/query` with CSV output that iOS can parse without a server-side shim). Timescale would need a separate API shim for the iOS client. Native packages fit the LXC-per-service philosophy — Docker on Proxmox NFS has storage and networking footguns we don't want.

## Three-bucket retention + downsample tasks (not continuous queries on one bucket)
**Decision:** Three physical buckets — `combsense` (raw, 30d), `combsense_1h` (365d), `combsense_1d` (∞) — populated by two Flux tasks.
**Why:** Per-bucket retention is enforced by Influx itself; we don't have to delete raw data manually. Tasks run independently, so a 1d task failure doesn't block raw ingest. Raw bucket disk footprint stays bounded at ~50 hives × 1 sample / 5 min × 30 days. 1-day bucket at ∞ retention costs almost nothing.

## No measurement rename in downsample tasks
**Decision:** Downsample tasks keep `_measurement = "sensor_reading"` in the target bucket — they do NOT rename to `sensor_reading_1h` / `sensor_reading_1d`.
**Why:** The iOS `HistoryService` Flux query hardcodes `r._measurement == "sensor_reading"` and switches buckets based on range. Renaming would force the app to know per-bucket measurement names, which leaks downsampling as an implementation detail into the client. Tried once with renames — all queries against downsampled buckets returned empty; debugged, reverted. The bucket IS the granularity signal.

## Flux literal durations over `-task.every * N`
**Decision:** Downsample task `range(start:)` uses literal durations (`-30m`, `-12h`) rather than computed expressions.
**Why:** Newer Flux (Influx 2.8+) rejects `duration * int` with "duration is not Divisible". Literal durations are also clearer about what window the task actually looks back at. Pay the cost of keeping `range` in sync with `task.every` manually — tasks change rarely.

## Single-broker Mosquitto (no per-yard brokers)
**Decision:** All MQTT traffic flows through one Mosquitto at 192.168.1.82. Per-yard brokers added only if/when cellular yards come online.
**Why:** One subscribe pattern in Telegraf (`combsense/hive/+/reading`) covers every sensor. Splitting brokers prematurely would duplicate Telegraf stanzas, token inventory, and ACL config for no benefit at current scale. Re-evaluate when the first remote yard lands.

## Three-token Influx auth (admin / telegraf-write / ios-read)
**Decision:** Three distinct tokens with least-privilege scopes. Admin token only used for setup and backup. Telegraf gets a write-only token scoped to the `combsense` bucket. iOS gets a read-only token for the whole org.
**Why:** The iOS token goes into a phone's Keychain — it must not be able to write. The Telegraf token lives on disk in config — it must not be able to read (or mint new tokens). Admin never leaves `/root/.combsense-tsdb-creds` on the LXC. Rotating any one doesn't require touching the others.

## Arrival-time stamping with firmware `t` preserved as field
**Decision:** Telegraf does NOT use firmware's `t` as the Influx timestamp. Points land at Telegraf arrival time. Firmware's `t` is stored as a queryable integer field (`sensor_ts`).
**Why:** Firmware emits `t=0` before its first NTP sync. Mapping `t=0` → epoch 1970 broke retention policy bounds (points rejected with 422). Alternative was a Telegraf starlark processor mapping `t==0` → `time.time()`; keeping arrival time is simpler and still correct for graphing. The `sensor_ts` field is there for forensic alignment when we drain offline-buffered readings — we can measure and bound the sense-to-record drift without it affecting the primary timeline.

## NTP sync in `drainBuffer()`, not `setup()`
**Decision:** Sensor-tag-wifi firmware calls `WifiManager::getUnixTime()` inside `drainBuffer()`, not on every wake's `setup()`. System clock persists across deep sleep via RTC.
**Why:** NTP requires WiFi. WiFi is already up during drain cycles (every N samples). Running NTP on every wake would require bringing WiFi up even on sample-only wakes, burning power. Samples taken before the first successful drain are tagged `t=0` and backfilled by the arrival-time stamping on the backend.

## Unprivileged LXC + sandboxing override drop-ins (no privileged containers)
**Decision:** `combsense-tsdb` runs as an unprivileged Proxmox LXC with per-service systemd drop-ins that permissively override sandboxing directives.
**Why:** Privileged containers have broader security implications than we want for an always-on home-infra box. Debian 12's stock systemd units ship with `PrivateMounts=true`, `ProtectSystem=strict`, etc., which can't be satisfied in the container's restricted user namespace — services fail with `226/NAMESPACE`. Drop-ins at `/etc/systemd/system/<svc>.service.d/override.conf` set each directive to its permissive value. Applied to `grafana-server` and `telegraf`; `systemd-logind` is masked outright since the box has no interactive tty logins.

## Branch workflow: dev → main via PR (no direct commits to main)
**Decision:** All work on `dev` (or feature branches off `dev`). Merge to `main` through PRs. User vocabulary: "prod" = `main`.
**Why:** Matches the iOS repo's existing split. `main` should always reflect what's deployed; in-progress work belongs somewhere it can be abandoned or force-pushed without touching the deployment baseline.
