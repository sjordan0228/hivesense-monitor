---
name: router
description: Session bootstrap and navigation hub. Read at the start of every session before any task.
last_updated: 2026-04-11
---

# Session Bootstrap

Read this file fully before doing anything else in this session.

## Current Project State

**Phase: All Three Firmwares Complete — Awaiting Hardware**

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

### Not yet built
- Phase 2: IR bee counter (8-pair beam-break array via CD74HC4067 mux)
- HiveSense iOS app sensor integration (BLE, MQTT, SensorReading model, SensorsTab)
- 3D printed enclosures and sensor gate
- HiveMQ Cloud account setup

**Related repos:**
- `sjordan0228/hivesense` — iOS app (SwiftUI + SwiftData)
- `sjordan0228/hivesense-monitor` — this repo (hardware + firmware)

## Routing Table

| Task type | Load |
|-----------|------|
| Understanding the hardware design | `README.md` (the full datasheet) |
| Working on ESP32 firmware | `firmware/hive-node/` directory |
| Working on collector firmware | `firmware/collector/` directory |
| Shared firmware headers | `firmware/shared/` directory |
| Making a design decision | `context/decisions.md` |
| Writing or reviewing code | `context/conventions.md` |

## Behavioural Contract

For every task, follow this loop:

1. **CONTEXT** — Load the relevant context file(s) from the routing table above.
2. **BUILD** — Do the work.
3. **VERIFY** — Build and test.
4. **GROW** — Update context files if anything changed.
