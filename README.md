# CombSense
### Smart Hive Monitoring System
**Technical Design & Hardware Datasheet — Rev 1.1 — 2026**

---

## Table of Contents
1. [System Overview](#1-system-overview)
2. [Hive Node Hardware](#2-hive-node-hardware)
3. [Power System](#3-power-system)
4. [Communication Architecture](#4-communication-architecture)
5. [Enclosure & Installation](#5-enclosure--installation)
6. [Bill of Materials](#6-bill-of-materials)
7. [Firmware Notes](#7-firmware-notes)
8. [Future Enhancements](#8-future-enhancements)

---

## 1. System Overview

CombSense is an IoT-based beehive telemetry system that uses a suite of sensors to continuously monitor hive health. Each hive node communicates wirelessly to a central yard collector which forwards data via cellular to an MQTT cloud broker. The CombSense iOS app subscribes to the broker to display real-time and historical data from anywhere. BLE provides direct access when at the yard without requiring internet.

The system is designed for remote apiary deployment with full solar-powered autonomous operation, requiring no mains power or on-site WiFi infrastructure.

| Layer | Description |
|---|---|
| **Hive Node** | One per hive. ESP32-WROOM-32 with all sensors. Communicates via ESP-NOW + BLE. |
| **Yard Collector** | One per apiary. LilyGO T-SIM7080G. Receives ESP-NOW from all nodes, forwards via cellular MQTT. |
| **Cloud Broker** | HiveMQ Cloud (free tier). MQTT broker — receives from collector, serves to app. |
| **iPhone App** | CombSense iOS app. Subscribes to MQTT for remote data. BLE for direct yard access. |

```
                              REMOTE (from anywhere)
Hive Node 1 ———┐
Hive Node 2 ———┤——→  Yard Collector  ——→  Cellular  ——→  HiveMQ Cloud (MQTT)
Hive Node 3 ———┤     (LilyGO T-SIM)                          ↓
Hive Node N ———┘                                        CombSense iOS App
                                                              ↑
                              AT THE YARD (direct)            │
Hive Node 1 ——————————————→  BLE  ——————————————————→  iPhone ——┘
```

### 1.1 Dual Communication Paths

| Path | When | Latency | Internet Required |
|---|---|---|---|
| **BLE direct** | At the yard, within 30-50 ft | Instant | No |
| **MQTT cloud** | Anywhere — home, travel | ~30 min (batch interval) | Yes (cellular + WiFi/cell on phone) |

BLE provides immediate access to stored sensor logs when visiting the yard. MQTT provides continuous monitoring from anywhere — alerts, trends, historical data — without needing to be present.

---

## 2. Hive Node Hardware

### 2.1 Microcontroller — ESP32-WROOM-32

The ESP32-WROOM-32 is the core of each hive node. It is a widely available, mature module combining the ESP32 dual-core processor with WiFi and BLE, chosen for its broad library support, ample GPIO count, low cost, and native ESP-NOW compatibility.

| Parameter | Value |
|---|---|
| Chip | ESP32-D0WDQ6 dual-core Xtensa LX6 |
| Clock | Up to 240 MHz (scaled to 80 MHz during IR scan) |
| Flash | 4 MB (PSRAM not included) |
| Wireless | 2.4 GHz WiFi 802.11 b/g/n + Bluetooth 4.2 / BLE |
| GPIO | 34 usable GPIO pins |
| Active current (no WiFi, 80 MHz) | ~20–25 mA |
| Active current (no WiFi, 240 MHz) | ~30–50 mA |
| Light sleep current | ~0.8 mA |
| Deep sleep current | ~10 µA (bare chip) |
| Power input | 3.3 V (module) / 5 V via dev board regulator |
| Form factor | 18 × 25.5 mm SMD module with PCB antenna |

> ℹ️ **Board vs bare module:** Deep sleep current on a dev board (e.g. ESP32 DevKit) is typically 3–5 mA due to onboard USB-UART chip and power LED. For production nodes use the bare ESP32-WROOM-32 module or cut the power LED to minimize sleep current.

---

### 2.2 Temperature & Humidity — Sensirion SHT31 (×2)

Two SHT31 sensors are used: one inside the hive body to monitor colony conditions, one external for ambient comparison. Both share the same I2C bus using different hardware addresses, eliminating the need for a multiplexer.

The SHT31 was chosen over the DHT22 for its built-in heater (critical for burning off condensation inside the hive's 50-80% humidity environment), superior accuracy, and reliable I2C interface. DHT22 sensors degrade within months in hive conditions; the SHT31's heater significantly extends sensor lifespan.

| Parameter | Value |
|---|---|
| Interface | I2C — addresses 0x44 (internal) and 0x45 (external) |
| Temperature accuracy | ±0.3°C |
| Humidity accuracy | ±2% RH |
| Temperature range | -40°C to +125°C |
| Humidity range | 0–100% RH |
| Active current | ~2 mA per sensor (~4 mA total) |
| Sleep current | ~2 µA per sensor |
| Onboard heater | Yes — burns off condensation, critical for hive interior use |
| Dual address support | Yes — two SHT31s on same bus without multiplexer |

---

### 2.3 Weight — HX711 ADC + 4× 50 kg Half-Bridge Load Cells

Hive weight is one of the most powerful colony health indicators, revealing nectar flow, swarm events, and winter preparation. Four half-bridge load cells wired as a full Wheatstone bridge provide accurate platform weighing up to 200 kg total capacity.

| Parameter | Value |
|---|---|
| Load cell type | Half-bridge ×4 wired as full Wheatstone bridge |
| Capacity per cell | 50 kg |
| Total platform capacity | 200 kg |
| ADC module | HX711 24-bit ADC |
| HX711 active current | ~1.5 mA |
| Load cell excitation current | ~1 mA under excitation |
| HX711 sleep current | ~1 µA (GPIO power gate recommended) |
| Interface | Custom 2-wire serial (CLK + DOUT) |

> ⚠️ **Wiring note:** The four half-bridge cells must be wired in the correct Wheatstone configuration. Two cells on opposing corners share E+ and the other two share E−. Incorrect orientation results in cancelled or near-zero output.

```
        E+ (red)
       /        \
  Cell 1        Cell 3
 (top-left)  (top-right)
       \        /
        A+ —— A−
       /        \
  Cell 2        Cell 4
(bottom-left) (bottom-right)
       \        /
        E− (black)
```

---

### 2.4 Bee Traffic Counter — 8-Pair IR Beam-Break Array

Eight infrared transmitter/receiver pairs are mounted across the 5-inch hive entrance, spaced approximately 0.625 inches apart. The array uses a hybrid multiplexing strategy to minimize power consumption while maintaining high counting accuracy.

**Recommended sensor:** [Adafruit IR Break Beam Sensor — 3mm LEDs (ADA2167)](https://www.adafruit.com/product/2167) — $2.95/pair. Emitter resistor is built in. Receiver requires a 10kΩ pull-up resistor to 3.3V for digital output.

| Parameter | Value |
|---|---|
| Sensor | Adafruit ADA2167 — 3mm IR break beam pair |
| Sensing distance | Up to 25 cm / 10" (well above the 0.625" gap needed) |
| Power voltage | 3.3–5.5V (run emitter from 5V rail for best signal) |
| Emitter current | 10 mA @ 3.3V / 20 mA @ 5V — resistor built in |
| Receiver output | Open collector NPN — pull-up handled via ESP32 internal pull-up on SIG_RX pin |
| LED angle | 10° — narrow beam, good lane isolation |
| Response time | < 2 ms |
| Cable length | 234 mm / 9.2" per half |
| Entrance coverage | 5 inches / ~127 mm |
| Beam spacing | ~0.625 inches / ~16 mm |
| Lane configuration | 4 directional lanes × 2 beams each (beam sequence determines direction) |
| Multiplexer IC | CD74HC4067 (×2) — 16-channel analog mux |
| GPIO required | 8 total (4 shared address lines + 2 enable + 2 signal) |
| Operating hours | Daytime only — MOSFET gated, RTC controlled |
| Night current | 0 mA (fully powered off) |

> ℹ️ **5V emitter note:** Power the emitter red wire from the 5V rail for maximum signal margin. The receiver signal wire feeds into the CD74HC4067 RX mux — pull-up is handled in firmware on the SIG_RX GPIO pin, no external resistors needed.

#### 2.4.1 CD74HC4067 Multiplexer Wiring

Two CD74HC4067 16-channel mux ICs handle the full array. The 4 address lines (S0–S3) are shared between both ICs so TX and RX always point to the same channel simultaneously — a matched pair fires and reads together.

```
ESP32 GPIO_S0 ——— CD74HC4067 TX (S0) + CD74HC4067 RX (S0)  (shared)
ESP32 GPIO_S1 ——— CD74HC4067 TX (S1) + CD74HC4067 RX (S1)  (shared)
ESP32 GPIO_S2 ——— CD74HC4067 TX (S2) + CD74HC4067 RX (S2)  (shared)
ESP32 GPIO_S3 ——— CD74HC4067 TX (S3) + CD74HC4067 RX (S3)  (shared)
ESP32 GPIO_EN_TX ——— CD74HC4067 TX enable
ESP32 GPIO_EN_RX ——— CD74HC4067 RX enable
ESP32 GPIO_SIG_TX ——— CD74HC4067 TX SIG (drives emitter)
ESP32 GPIO_SIG_RX ——— CD74HC4067 RX SIG (reads receiver via ADC/GPIO)

Channels 0–7: IR emitter/receiver pairs 1–8
Channels 8–15: available for future expansion (up to 16 pairs total)
```

#### 2.4.2 Lane Layout

```
|  Lane 1  |  Lane 2  |  Lane 3  |  Lane 4  |
| [A1][A2] | [B1][B2] | [C1][C2] | [D1][D2] |
———————————————— 5 inches ————————————————————

Bee going IN:  breaks beam A1 → then A2
Bee going OUT: breaks beam A2 → then A1
```

#### 2.4.3 Hybrid Multiplexing Strategy

The IR array operates in two automatically switched modes based on detected traffic density. Mode switching is handled entirely in firmware — no additional hardware required.

| Mode | Trigger | Avg Current | Count Accuracy | Direction Accuracy |
|---|---|---|---|---|
| **Multiplex** (light traffic) | Default / 0–1 beams broken | ~1.3 mA | ~99% | ~97% |
| **Group mode** (heavy traffic) | 2+ beams broken simultaneously | ~3.0 mA | ~96% | ~92% |

In multiplex mode, one LED is pulsed at a time in rotation (~200 scans/second). In group mode, lane pairs (2 beams) are powered together for improved temporal resolution during peak foraging periods.

---

## 3. Power System

### 3.1 Power Sources

| Component | Spec | Notes |
|---|---|---|
| Solar panel | 5V, 2W (400 mA peak) | Mount on south-facing side of hive — yields ~5–6 effective sun hours/day |
| Battery | 18650 LiPo, 3000–3500 mAh | ~80% usable capacity after voltage cutoff (~2,560–2,800 mAh usable) |
| Solar harvest/day | ~1,400 mAh | 400 mA × 5 hrs × 70% efficiency |
| Daily consumption | ~55–100 mAh | Varies by IR traffic load and daytime hours |

### 3.2 Operating Modes & Current Draw

| Mode | Period | Current | Notes |
|---|---|---|---|
| Deep sleep (night) | ~10 hrs | ~0.8–2 mA | ~10 µA bare chip — board overhead depends on design |
| IR scan + light sleep (day) | ~14 hrs | ~3–5 mA | CPU at 80 MHz, automatic light sleep between scans (~0.8 mA light sleep) |
| Sensor read + ESP-NOW TX | ~6–10 sec every 30 min | ~26 mA | Day and night |
| BLE advertisement | When phone detected | ~12 mA | Short burst during yard visit |
| **No-sun battery reserve** | — | **~40+ days** | At ~55 mAh/day with 2,560 mAh usable |

### 3.3 Daily Power Budget Breakdown

```
Component               Avg mA      mAh/day
——————————————————————————————————————————————
ESP32-WROOM-32          ~2.0 mA     ~48 mAh
IR array (hybrid, 8hr)  ~0.6 mA     ~14 mAh
SHT31 x2                ~0.1 mA     ~2 mAh
HX711 + load cells      ~0.05 mA    ~1 mAh
CD74HC4067 x2           ~0.05 mA    ~1 mAh
——————————————————————————————————————————————
Total (typical)                     ~66 mAh
Solar harvest (5hr)                 ~1,400 mAh
Net surplus/day                     ~1,334 mAh
```

---

## 4. Communication Architecture

### 4.1 Overview — Dual Path

The system supports two communication paths operating simultaneously:

| Path | Protocol | Range | Internet | Use Case |
|---|---|---|---|---|
| **Direct** | BLE 4.2 | ~30-50 ft | No | At the yard — instant data pull, sensor config |
| **Remote** | ESP-NOW → Cellular → MQTT | Unlimited | Yes | From anywhere — continuous monitoring, alerts |

### 4.2 Path 1: BLE Direct (At the Yard)

Each hive node runs a BLE peripheral server that advertises when it has stored data. When the CombSense iOS app comes within range, it discovers the node, connects, and downloads stored sensor logs.

| Parameter | Value |
|---|---|
| Protocol | BLE 4.2 (GATT server on ESP32) |
| Range | ~30-50 ft line of sight |
| Data transfer | Bulk download of stored readings since last sync |
| Pairing | First-time: hold phone near hive, tap "Pair Sensor" in app |
| Post-sync | ESP32 clears downloaded data from flash |
| Power impact | Minimal — BLE advertisement only when data available |

**BLE flow:**
1. ESP32 advertises "CombSense-Hive3" with beacon containing reading count
2. CombSense app discovers beacons while in Yard Mode or Scan tab
3. App shows "3 hives with sensor data available"
4. User taps "Sync All" — app connects to each ESP32, downloads readings
5. Data saved to SwiftData — shows graphs in hive detail screen
6. ESP32 clears synced data from flash

### 4.3 Path 2: ESP-NOW → Yard Collector → MQTT Cloud (Remote)

ESP-NOW is Espressif's connectionless peer-to-peer protocol operating on the 2.4 GHz radio without requiring a router or access point. Each hive node targets the collector's fixed MAC address directly.

| Parameter | Value |
|---|---|
| Protocol | Espressif ESP-NOW (IEEE 802.11 based) |
| Range (open field) | ~200 m line of sight |
| Transmission interval | Every 30 minutes |
| TX attempts per cycle | 3 attempts with 2-second gap (RTC drift tolerance) |
| TX duration | ~100 ms per burst |

#### 4.3.1 Yard Collector — LilyGO T-SIM7080G

The LilyGO T-SIM7080G serves as the yard aggregator and cellular gateway. It receives ESP-NOW packets from all hive nodes and batches them into a single cellular MQTT connection every 30 minutes, minimizing modem-on time and data usage.

| Parameter | Value |
|---|---|
| Core chip | ESP32-S3 (ESP-NOW native compatible) |
| Cellular modem | SIM7080G — LTE-M + NB-IoT + GPS |
| Supported networks | LTE-M / NB-IoT on AT&T, T-Mobile, Verizon LTE infrastructure |
| SIM provider | Hologram (recommended) — $1/month + negligible data |
| Data usage | < 0.5 MB/month per yard (5 hives, 30-min intervals) |
| GPS | Built-in — useful for theft/location tracking |

#### 4.3.2 MQTT Cloud Broker — HiveMQ Cloud

HiveMQ Cloud provides a managed MQTT broker with a free tier sufficient for CombSense.

| Parameter | Value |
|---|---|
| Provider | HiveMQ Cloud (free tier) |
| Free tier limits | 100 connections, 10 GB/month |
| Protocol | MQTT 3.1.1 / 5.0 over TLS |
| Uptime SLA | 99.95% |
| Cost | Free for v1 scale |
| Alternative options | CloudMQTT, AWS IoT Core, Mosquitto on VPS ($5/mo) |

The collector publishes sensor data to HiveMQ. The CombSense iOS app subscribes to receive updates in near-real-time.

#### 4.3.3 MQTT Topic Schema

| Topic | Payload | Type |
|---|---|---|
| `combsense/hive/{id}/weight` | kg | Float |
| `combsense/hive/{id}/temp/internal` | °C (inside hive body) | Float |
| `combsense/hive/{id}/temp/external` | °C (ambient) | Float |
| `combsense/hive/{id}/humidity/internal` | % RH | Float |
| `combsense/hive/{id}/humidity/external` | % RH | Float |
| `combsense/hive/{id}/bees/in` | Cumulative count since last reset | Integer |
| `combsense/hive/{id}/bees/out` | Cumulative count since last reset | Integer |
| `combsense/hive/{id}/bees/activity` | Rolling 30-min window count | Integer |
| `combsense/hive/{id}/battery` | Battery % estimated from voltage | Integer |
| `combsense/hive/{id}/rssi` | ESP-NOW signal strength to collector | Integer |

#### 4.3.4 iOS App MQTT Integration

The CombSense iOS app uses an MQTT client library (e.g., CocoaMQTT) to subscribe to the broker:

| Feature | Implementation |
|---|---|
| Library | CocoaMQTT (Swift, SPM compatible) |
| Connection | TLS to HiveMQ Cloud endpoint |
| Subscribe | `combsense/hive/+/#` — all hives, all topics |
| Data storage | Incoming readings saved to SwiftData `SensorReading` model |
| Background | iOS background fetch for periodic pull when app is closed |
| Notifications | Push alerts for critical events (weight drop, temp anomaly) |

---

## 5. Enclosure & Installation

### 5.1 Node Enclosure

- IP65 or IP67 rated ABS weatherproof project box recommended for initial builds
- Mount on the **shaded side** of the hive body (north face preferred) to minimize solar heat gain inside the enclosure
- Use standoffs or spacers to create an **air gap** between the enclosure and hive wall — breaks thermal contact with hive body heat (~35°C colony temp) and improves ambient sensor accuracy
- Cable glands on the enclosure base for all sensor wiring — maintains IP rating
- ASA or PETG 3D-printed custom enclosure is a practical upgrade once the form factor is validated — **do not use PLA** (degrades under UV and summer heat)

### 5.2 Sensor Placement

| Sensor | Location | Notes |
|---|---|---|
| SHT31 (external) | Inside enclosure with vent mesh | Air gap from hive body prevents false elevated readings |
| SHT31 (internal) | Probe wire through bottom board into hive body, mounted under inner cover in stainless mesh cage | Mesh prevents propolis coating; may need seasonal cleaning |
| HX711 + load cells | Under hive bottom board | 4-corner platform — all cells must be level and oriented correctly |
| IR array | Front of entrance on landing board as sensor gate | 15mm tall opening — bees walk through; does not reduce hive entrance height |

### 5.3 IR Sensor Gate

The IR array mounts as a separate gate on the landing board in front of the hive entrance — not inside it. This preserves full entrance height.

```
    Side view:
    
    HIVE BODY
    ——————————————————————————
                    ┌— emitters (top bar, 8mm)
    entrance ———→   │  15mm opening (bees walk through)
                    └— receivers (bottom bar, 8mm)
    ——————————————————————————
    LANDING BOARD ════════════
    
    Total gate height: ~31mm (1.25 inches)
    Bee opening: 15mm (5/8 inch) — accommodates workers and drones
    Gate width: ~5 inches (with entrance reducers on sides)
```

---

## 6. Bill of Materials

### 6.1 Per Hive Node

| Component | Part / Spec | Qty | Est. Cost |
|---|---|---|---|
| Microcontroller | ESP32-WROOM-32 bare module | 1 | ~$3 |
| Temp/humidity sensor | Sensirion SHT31 breakout | 2 | ~$10 |
| Load cell ADC | HX711 module | 1 | ~$2 |
| Load cells | 50 kg half-bridge | 4 | ~$8 |
| IR break beam sensor | Adafruit ADA2167 — 3mm IR pair | 8 | ~$24 |
| Multiplexer | CD74HC4067 16-channel analog mux | 2 | ~$2 |
| Battery | 18650 LiPo 3000–3500 mAh | 1 | ~$10 |
| Charge controller | TP4056 module | 1 | ~$1 |
| Solar panel | 5V 2W (400 mA peak) | 1 | ~$10 |
| Enclosure | IP65 weatherproof ABS box | 1 | ~$8 |
| Resistors, cable glands, wire, connectors | — | — | ~$5 |
| **Total per hive node** | | | **~$83** |

### 6.2 Yard Collector (one per apiary)

| Component | Part / Spec | Qty | Est. Cost |
|---|---|---|---|
| Collector board | LilyGO T-SIM7080G | 1 | ~$28 |
| SIM card | Hologram IoT SIM | 1 | $3 one-time |
| Monthly cellular | Hologram — $1/month + data | — | ~$1/month |
| Battery + solar | Larger LiPo + 5–10W panel | 1 | ~$25 |
| Enclosure | IP65 weatherproof | 1 | ~$10 |
| **Total per yard** | | | **~$66 + $1/mo** |

### 6.3 Cloud Services

| Service | Provider | Cost |
|---|---|---|
| MQTT broker | HiveMQ Cloud (free tier) | $0 |
| Future upgrade | Self-hosted Mosquitto on $5/mo VPS | $5/mo |

### 6.4 Total System Cost

| Deployment | Cost |
|---|---|
| 1 hive + 1 collector | ~$149 |
| 8 hives + 1 collector | ~$730 |
| 20 hives + 1 collector | ~$1,726 |
| Monthly recurring | ~$1/yard (cellular) |

---

## 7. Firmware Notes

### 7.1 Hive Node Operating State Machine

| State | Trigger | Actions |
|---|---|---|
| **Deep sleep** | Nighttime RTC window | All peripherals off. Wake every 30 min for sensor read + ESP-NOW TX. |
| **IR scan mode** | Daytime RTC window | CPU at 80 MHz. Automatic light sleep between scans. IR array powered via MOSFET. |
| **Sensor read** | 30-min timer | Wake fully. Read SHT31 ×2, HX711, IR counts. ~6–10 seconds. |
| **ESP-NOW TX** | After sensor read | Spin up radio. Transmit 3 attempts with 2-sec gap. Confirm ACK. Return to prior state. |
| **BLE server** | Phone detected nearby | Advertise stored data count. Accept connection. Transfer logs. Clear synced data. |

```
12am ———————————————— 6am    Deep sleep     ~2 mA
6am  ———————————————— 8pm    IR scan mode   ~4–6 mA   (daytime)
8pm  ———————————————— 12am   Deep sleep     ~2 mA
                    ↕
          Every 30 min regardless of state:
          sensor read + ESP-NOW TX burst (~26 mA for ~8 sec)
          
          BLE: on-demand when phone in range
```

### 7.2 Key Firmware Requirements

- **GPIO power gate** (MOSFET) for HX711 and load cells — no excitation current during sleep
- **IR array MOSFET gate** controlled by RTC-based daytime window
- **ESP-NOW collector MAC** stored in firmware flash — fixed per yard deployment
- **Bee counts in RTC memory** to survive light sleep cycles — reset on deep sleep wakeup
- **Battery voltage via ADC** read before each transmit — include in MQTT payload
- **CPU frequency scaling:** 240 MHz during boot/TX → 80 MHz during IR scan → minimum during sensor reads
- **Automatic light sleep** enabled during IR scan loop — WROOM light sleep (~0.8 mA)
- **Disable WiFi/BT** before deep sleep — call `esp_wifi_stop()` and `esp_bt_controller_disable()`
- **Internal pull-up on SIG_RX pin only** — enables `INPUT_PULLUP` on the CD74HC4067 RX SIG GPIO, eliminating all external pull-up resistors
- **BLE GATT server** — exposes sensor log characteristic for bulk download
- **Flash storage** — circular buffer of sensor readings, cleared after BLE sync

```c
// Enable internal pull-up on RX SIG pin only
pinMode(SIG_RX_PIN, INPUT_PULLUP);
```

### 7.3 ESP-NOW Payload Struct

```c
typedef struct {
  char     hive_id[16];
  float    weight_kg;
  float    temp_internal;
  float    temp_external;
  float    humidity_internal;
  float    humidity_external;
  uint16_t bees_in;
  uint16_t bees_out;
  uint16_t bees_activity;
  uint8_t  battery_pct;
  int8_t   rssi;
} HivePayload;
```

### 7.4 BLE GATT Service

```
Service UUID: 0xHVSN (custom)
├── Characteristic: Sensor Log (read, notify)
│   └── Array of HivePayload structs (bulk transfer)
├── Characteristic: Reading Count (read)
│   └── uint16_t — number of stored readings
├── Characteristic: Hive ID (read/write)
│   └── char[16] — maps to CombSense app hive
└── Characteristic: Clear Log (write)
    └── uint8_t — write 0x01 to clear after sync
```

### 7.5 Sensor Tag — WiFi Variant (home yards)

For beekeepers with hives in WiFi range of their home network, `firmware/sensor-tag-wifi/` is a fork of the BLE tag that publishes directly to a local Mosquitto broker over MQTT — no collector required.

**Hardware:** XIAO ESP32-C6 + 2× DS18B20 (or SHT31 pair) + 18650 Li-ion + 100 mAh solar panel + TP4056/DW01 charger
**Power:** 5-min sample cadence, solar-maintained — runs indefinitely with daylight
**Transport:** WiFi → MQTT → Mosquitto at the local IP
**Topic:** `combsense/hive/<device-id>/reading`
**Payload:** JSON — `{"id":"ab12cd34","t":1712345678,"t1":22.4,"t2":24.1,"h1":52.3,"h2":55.1,"b":87}`
**Build variants:**
- `pio run -e xiao-c6-sht31` — dual SHT31 (temp + humidity, brood + top)
- `pio run -e xiao-c6-ds18b20` — dual DS18B20 (temp only, brood + top)

**Provisioning:** connect to serial @115200 during boot window. Set `wifi_ssid`, `wifi_pass`, `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_pass`. Optional: `sample_int` (seconds), `upload_every` (samples).

---

## 8. CombSense iOS App Integration Spec

This section defines what must be built in the CombSense iOS app (separate repo: `sjordan0228/combsense`) to receive, store, and display sensor data from the monitoring hardware.

### 8.1 Existing App Context

The CombSense iOS app is a SwiftUI + SwiftData app (iOS 17+) for beekeepers. It already has:
- Yard and Hive models in SwiftData with full CRUD
- NFC tag read/write for hive identification
- Voice-based inspections via WhisperKit
- Inspection, Feeding, MiteCount, Cost, Treatment record forms
- Subscription tiers (Free/Pro/Sideliner/Commercial) via StoreKit 2
- Photo capture and storage
- Reports with SwiftData aggregations

The IoT monitoring adds a new data layer — continuous sensor readings alongside the existing manual inspection data.

### 8.2 New SwiftData Model — SensorReading

```swift
@Model
final class SensorReading {
    var id: UUID
    var timestamp: Date
    var weightKg: Double?
    var tempInternal: Double?     // °C
    var tempExternal: Double?     // °C
    var humidityInternal: Double? // % RH
    var humidityExternal: Double? // % RH
    var beesIn: Int?
    var beesOut: Int?
    var beesActivity: Int?        // rolling 30-min count
    var batteryPercent: Int?
    var source: ReadingSource      // .ble or .mqtt

    var hive: Hive?

    init(timestamp: Date, source: ReadingSource) {
        self.id = UUID()
        self.timestamp = timestamp
        self.source = source
    }
}

enum ReadingSource: UInt8, Codable, CaseIterable {
    case ble = 0
    case mqtt = 1
}
```

Relationship: `Hive` gets a new `@Relationship(deleteRule: .cascade, inverse: \SensorReading.hive) var sensorReadings: [SensorReading]`

### 8.3 BLE Integration — CoreBluetooth

**Service:** CombSenseBLEService (new file)
- Scans for peripherals advertising the CombSense service UUID
- Discovers hive nodes, reads their hive ID characteristic
- Matches to existing Hive in SwiftData by hiveId
- Downloads sensor log characteristic (array of readings)
- Writes clear command after successful download
- Uses `CBCentralManager` and `CBPeripheral` delegates

**BLE Flow in App:**
1. User opens Yard Mode or Scan tab → app starts BLE scan
2. Discovers CombSense peripherals → shows "3 hives with sensor data"
3. User taps "Sync Sensors" → connects to each, downloads logs
4. Creates `SensorReading` entries in SwiftData, linked to matching Hive
5. Sends clear command to ESP32 → frees flash storage

**Pairing Flow:**
1. User goes to YardDetailScreen → taps "Pair Sensor" on a hive
2. App scans for unpaired CombSense BLE devices
3. User selects the device (identified by signal strength / proximity)
4. App writes the hive's `hiveId` to the BLE device's Hive ID characteristic
5. Stores the device's MAC address on the Hive model for future auto-connect

**New Hive model properties:**
```swift
var sensorMacAddress: String?  // BLE MAC of paired ESP32
var lastSensorSync: Date?      // when data was last pulled
```

### 8.4 MQTT Integration — CocoaMQTT

**Dependency:** CocoaMQTT (Swift, SPM compatible)

**Service:** MQTTService (new file, @MainActor ObservableObject)
- Connects to HiveMQ Cloud endpoint via TLS
- Subscribes to `combsense/hive/+/#`
- Parses incoming messages into `SensorReading` entries
- Resolves hive ID from topic path → links to SwiftData Hive
- Runs as background service, reconnects on disconnect

**Configuration stored in @AppStorage:**
```swift
@AppStorage("mqttBrokerHost") var brokerHost: String = ""
@AppStorage("mqttBrokerPort") var brokerPort: Int = 8883
@AppStorage("mqttUsername") var mqttUsername: String = ""
@AppStorage("mqttPassword") var mqttPassword: String = ""  // should use Keychain in production
@AppStorage("mqttEnabled") var mqttEnabled: Bool = false
```

**Settings UI:** New "Cloud Monitoring" section in SettingsScreen
- Toggle to enable/disable MQTT
- Broker host, port, username, password fields
- Connection status indicator
- "Test Connection" button

### 8.5 UI — Sensor Tab on HiveDetailScreen

Add a 6th tab "Sensors" to the HiveDetailScreen segmented picker.

**SensorsTab layout:**
```
┌─────────────────────────────────┐
│ Weight Trend (7-day chart)      │
│ ┌─────────────────────────────┐ │
│ │  📈 Line chart              │ │
│ │  Current: 85.2 kg           │ │
│ │  Change: +2.3 kg this week  │ │
│ └─────────────────────────────┘ │
│                                 │
│ Temperature & Humidity          │
│ ┌──────────┐ ┌──────────┐      │
│ │ Internal │ │ External │      │
│ │ 34.8°C   │ │ 28.2°C   │      │
│ │ 62% RH   │ │ 45% RH   │      │
│ └──────────┘ └──────────┘      │
│                                 │
│ Bee Traffic (today)             │
│ ┌─────────────────────────────┐ │
│ │  In: 4,230   Out: 4,180    │ │
│ │  Net: +50    Activity: 847 │ │
│ └─────────────────────────────┘ │
│                                 │
│ Battery: 87% ████████░░        │
│ Last sync: 2 min ago            │
└─────────────────────────────────┘
```

**Charts:** Use Swift Charts framework (iOS 16+) for weight trend lines and bee traffic bar charts.

### 8.6 Alerts & Notifications

**Alert conditions (configurable thresholds in Settings):**

| Alert | Default Trigger | Severity |
|---|---|---|
| Weight drop | > 3 kg in 1 hour | Critical (possible swarm) |
| Weight drop | > 1 kg/day sustained | Warning (consuming stores) |
| Internal temp drop | Below 32°C (90°F) | Warning (weak cluster) |
| Internal temp spike | Above 40°C (104°F) | Warning (overheating) |
| Humidity high | Above 80% RH internal | Warning (moisture/disease risk) |
| Bee traffic zero | No activity for 2+ hours during daytime | Critical (colony loss) |
| Battery low | Below 20% | Info |
| Sensor offline | No data for 2+ hours | Warning |

**Implementation:**
- `SensorAlertService` checks incoming readings against thresholds
- Triggers local notifications via `UNUserNotificationCenter`
- Shows alert badges on Dashboard "Alerts" metric card
- Alerts displayed on HiveDetailScreen sensors tab

### 8.7 Dashboard Integration

The existing DashboardScreen metrics row gets updated:
- **"Sensors"** metric card showing how many hives have active sensor nodes
- **Alerts** count includes sensor-triggered alerts
- **Needs Attention** section includes sensor anomalies

### 8.8 Files to Create in CombSense iOS App

| File | Purpose |
|---|---|
| `CombSense/Models/SensorReading.swift` | SwiftData model |
| `CombSense/Models/Enums/ReadingSource.swift` | BLE vs MQTT source enum |
| `CombSense/Services/CombSenseBLEService.swift` | CoreBluetooth BLE client |
| `CombSense/Services/MQTTService.swift` | CocoaMQTT wrapper |
| `CombSense/Services/SensorAlertService.swift` | Threshold checking + notifications |
| `CombSense/Features/Hive/SensorsTab.swift` | Sensor data display with charts |
| `CombSense/Features/Settings/CloudMonitoringView.swift` | MQTT configuration UI |
| `CombSense/Features/Yard/SensorSyncView.swift` | BLE sync UI for yard visits |

### 8.9 Dependencies to Add

| Package | Purpose | SPM URL |
|---|---|---|
| CocoaMQTT | MQTT client | `https://github.com/emqx/CocoaMQTT.git` |
| Swift Charts | Already in iOS 16+ SDK | Built-in framework |
| CoreBluetooth | BLE | Built-in framework |

---

## 9. Future Enhancements

- **RFID hive identification** — NFC tag per hive for automatic node-to-hive association (already built in CombSense app)
- **Audio monitoring** — microphone + FFT analysis for swarm detection via colony sound signature changes
- **TFT display on collector** — local yard status without needing phone
- **Predictive swarming alerts** — ML model trained on weight + bee activity patterns to predict swarm events 24–48 hrs ahead
- **Multi-yard support** — multiple collectors reporting to same MQTT broker, differentiated by yard ID in topic path
- **OTA firmware updates** — push firmware updates to hive nodes via collector relay
- **CombSense app sensor tab** — weight/temp/humidity charts on HiveDetailScreen, BLE sync UI, alert configuration

---

*CombSense Technical Datasheet — Rev 1.1 — 2026 — Internal Design Document*
*Updated: Added BLE direct communication, HiveMQ Cloud MQTT broker, iOS app integration, sensor gate installation details*
