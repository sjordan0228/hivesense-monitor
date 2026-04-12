---
name: router
description: Session bootstrap and navigation hub. Read at the start of every session before any task.
last_updated: 2026-04-11 (self-OTA module added)
---

# Session Bootstrap

Read this file fully before doing anything else in this session.

## Current Project State

**Phase: Phase 1 Firmware Complete + OTA — ESP32-S3, Awaiting Hardware**

### Completed
- Hardware datasheet and design spec (README.md)
- Phase 1 hive node firmware (`firmware/hive-node/`) — Freenove ESP32-S3 Lite (8MB flash)
  - State machine dispatcher with power-aware sleep/wake cycle
  - SHT31 dual temp/humidity (internal 0x44 + external 0x45)
  - HX711 weight with NVS calibration and MOSFET gating
  - Battery ADC with calibrated voltage-to-percent
  - ESP-NOW transmit with 3-retry logic
  - BLE GATT server (NimBLE) with sensor log sync, pairing, and log clear
  - LittleFS circular buffer storage (500 readings)
  - Deep sleep (nighttime) + light sleep (daytime) power management
  - OTA update receive module with CRC32 validation and auto-rollback
  - Shared headers (`firmware/shared/`) for HivePayload and OTA protocol
- Build: 27.5% flash (1.0 MB of 3.5 MB), 15.4% RAM, OTA dual partition (8MB flash)

### Not yet built
- Phase 2: IR bee counter (8-pair beam-break array via CD74HC4067 mux)
- Yard collector firmware (LilyGO T-SIM7080G) — ESP-NOW receiver, cellular module, MQTT publisher, and self-OTA module done; OTA relay module (forwarding updates to hive nodes) remaining
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
| Working on collector firmware | `firmware/collector/` directory (cellular module live; MQTT + OTA relay remaining) |
| Shared firmware headers | `firmware/shared/` directory |
| Making a design decision | `context/decisions.md` |
| Writing or reviewing code | `context/conventions.md` |

## Behavioural Contract

For every task, follow this loop:

1. **CONTEXT** — Load the relevant context file(s) from the routing table above.
2. **BUILD** — Do the work.
3. **VERIFY** — Build and test.
4. **GROW** — Update context files if anything changed.
