# Wireless Sensor Tag — Design Spec

**Date:** 2026-04-12
**Scope:** DIY wireless BLE sensor tag for internal hive temp/humidity, plus hive node BLE scanner to receive tag data. Eliminates wires into the hive body.
**Prototype hardware:** Seeed XIAO ESP32C3 + SHT31 + CR2032
**Production target:** Custom PCB, 25×30mm, 2+ year coin cell life

---

## 1. System Overview

A small battery-powered tag sits inside the hive between frames. It wakes every 60 seconds, reads temperature and humidity, broadcasts a BLE advertisement, and goes back to deep sleep. The main hive node (ESP32-S3) scans for this advertisement during its wake cycle and uses the data as the internal sensor reading.

```
Inside Hive                          Outside Hive (enclosure)
┌─────────────┐     BLE advert      ┌──────────────────────┐
│ Sensor Tag  │ ─────────────────→  │ Hive Node (ESP32-S3) │
│ (ESP32-C3)  │                     │                      │
│ SHT31       │                     │ SHT31 (external)     │
│ CR2032      │                     │ HX711 + load cells   │
└─────────────┘                     │ ESP-NOW, BLE GATT    │
                                    └──────────────────────┘
```

The tag replaces the wired internal SHT31. The external SHT31 on the hive node enclosure still provides ambient readings via I2C.

---

## 2. Sensor Tag Firmware

### Project Structure

```
firmware/sensor-tag/
├── platformio.ini
├── include/
│   └── config.h        — Tag name, interval, I2C pins
└── src/
    └── main.cpp        — Wake, read, advertise, sleep
```

### platformio.ini

```ini
[env:xiao-c3]
platform = espressif32@^6.5.0
board = seeed_xiao_esp32c3
framework = arduino
monitor_speed = 115200

lib_deps =
    adafruit/Adafruit SHT31 Library@^2.2.2
    h2zero/NimBLE-Arduino@^1.4.0

build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DCONFIG_BT_NIMBLE_ROLE_BROADCASTER
    -DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL=0
    -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=0
    -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=0
```

### Operating Cycle

1. Wake from deep sleep (timer, 60 seconds)
2. Read SHT31 via I2C
3. Read battery voltage via ADC
4. Pack data into BLE manufacturer-specific advertisement
5. Start NimBLE advertising
6. Advertise for 200 ms (multiple advertisement events at default interval)
7. Stop advertising, deinit BLE
8. Enter deep sleep for ~60 seconds
9. Repeat

### BLE Advertisement Format

Device name: configurable, default `"HiveSense-Tag-001"`

Manufacturer-specific data payload:

| Byte | Field | Type | Description |
|---|---|---|---|
| 0-1 | Manufacturer ID | uint16 | 0xFFFF (prototyping / unregistered) |
| 2 | Version | uint8 | 0x01 |
| 3-4 | Temperature | int16 | (temp_C + 40.0) × 100 — supports -40 to +125°C |
| 5-6 | Humidity | uint16 | humidity_RH × 100 — supports 0-100% |
| 7 | Battery | uint8 | Percentage 0-100 |

Decoding on hive node:
- Temperature: `(raw_int16 / 100.0) - 40.0` = °C
- Humidity: `raw_uint16 / 100.0` = %RH

### I2C Pins (XIAO ESP32C3)

| Pin | Function |
|---|---|
| D4 (GPIO 6) | SDA |
| D5 (GPIO 7) | SCL |

### Tag Configuration via Serial Console

Reuse the shared `serial_console` module. On boot, 3-second window to enter console:

| Key | Type | Default | Purpose |
|---|---|---|---|
| `tag_name` | string | "HiveSense-Tag-001" | BLE device name |
| `adv_interval_sec` | uint8 | 60 | Seconds between advertisements |

---

## 3. Power Budget

### Prototype (XIAO ESP32C3 dev board)

| State | Duration | Current | Notes |
|---|---|---|---|
| Deep sleep | ~59.8 s | ~40-50 µA | USB chip + regulator overhead |
| Active (read + advertise) | ~200 ms | ~15 mA | |

Daily: ~1.3 mAh sleep + ~0.07 mAh active = ~1.4 mAh/day
CR2032 (220 mAh): **~5 months** — fine for prototyping.

### Production (custom PCB, bare ESP32-C3-MINI-1)

| State | Duration | Current | Notes |
|---|---|---|---|
| Deep sleep | ~59.8 s | ~7 µA | 5 µA C3 + 2 µA SHT31 idle |
| Active (read + advertise) | ~200 ms | ~15 mA | |

Daily: ~0.17 mAh sleep + ~0.07 mAh active = ~0.24 mAh/day
CR2032 (220 mAh): **~900 days (~2.5 years)** conservatively. With battery self-discharge factored in, expect **2+ years** real-world.

---

## 4. Hive Node Changes

### Enable BLE Observer Role

In `firmware/hive-node/platformio.ini`, change:
```
-DCONFIG_BT_NIMBLE_ROLE_OBSERVER=0
```
to:
```
-DCONFIG_BT_NIMBLE_ROLE_OBSERVER
```

Flash cost: ~50-100 KB additional. Current app is 1.0 MB of 3.5 MB — plenty of headroom.

### New Module: ble_tag_reader.cpp/.h

```cpp
namespace BleTagReader {
    /// Scan for a sensor tag by name. Timeout in milliseconds.
    /// Returns true if tag was found and data populated.
    bool scan(uint16_t timeoutMs);

    /// Get last received temperature (°C). NaN if no tag found.
    float getTemperature();

    /// Get last received humidity (%RH). NaN if no tag found.
    float getHumidity();

    /// Get last received battery percentage. 0 if no tag found.
    uint8_t getBattery();
}
```

### State Machine Integration

In `state_machine.cpp`, during `SENSOR_READ` state — after battery read, before SHT31 read:

```cpp
// Read internal temp/humidity from wireless sensor tag
if (BleTagReader::scan(5000)) {
    payload.temp_internal = BleTagReader::getTemperature();
    payload.humidity_internal = BleTagReader::getHumidity();
}
// External SHT31 on enclosure provides temp_external + humidity_external
```

The BLE scan runs for 5 seconds. If no tag is found (tag_name not set in NVS, or tag out of range), the fields stay at 0 — same as current behavior without a wired internal sensor.

### NVS Configuration

New key added to hive node serial console:

| Key | Type | Default | Purpose |
|---|---|---|---|
| `tag_name` | string | "" | BLE tag name to listen for. Empty = skip scan. |

### Timing Impact

The 5-second BLE scan adds to the wake cycle duration:

| Before | After |
|---|---|
| Sensor read: ~3 sec | Sensor read: ~8 sec (3 + 5 BLE scan) |
| Total wake: ~15 sec | Total wake: ~20 sec |

Power impact: ~12 mA × 5 sec = 0.017 mAh per wake cycle. At 48 cycles/day = 0.8 mAh/day additional. Negligible against the solar panel's 700+ mAh/day harvest.

---

## 5. Directory Structure Update

```
firmware/
├── hive-node/       — ESP32-S3 hive node (existing)
├── collector/       — LilyGO T-SIM7080G (existing)
├── sensor-tag/      — ESP32-C3 wireless sensor tag (NEW)
│   ├── platformio.ini
│   ├── include/
│   │   └── config.h
│   └── src/
│       └── main.cpp
└── shared/          — Shared headers (existing)
    ├── hive_payload.h
    ├── ota_protocol.h
    ├── espnow_protocol.h
    ├── serial_console.h
    └── serial_console.cpp
```

The sensor tag reuses `serial_console` from shared for provisioning.

---

## 6. Scope — What Gets Built Now

### This Implementation
- Sensor tag firmware (`firmware/sensor-tag/`)
- BLE tag reader module on hive node (`ble_tag_reader.cpp/.h`)
- Enable NimBLE observer role on hive node
- State machine integration (scan during SENSOR_READ)
- Serial console `tag_name` key on both devices
- Update GitHub Actions CI for third firmware target

### Deferred
- BroodMinder T2SM compatibility (can add later as a secondary scan filter)
- Custom PCB design (after firmware validated on XIAO dev board)
- Production coin cell power optimization

---

## 7. Bill of Materials (Prototyping)

| Item | Qty | Est. Price |
|---|---|---|
| Seeed XIAO ESP32C3 | 2 | ~$10 |
| HiLetgo SHT31-D breakout | (on order) | $17 |
| CR2032 coin cell | 4-pack | ~$4 |
| CR2032 holder (through-hole) | 2 | ~$3 |
| Jumper wires + breadboard | (on hand) | $0 |
| **Total** | | **~$34** |

One SHT31 for the tag, one for the external sensor on the hive node enclosure.
