# HiveSense iOS App — Sensor Integration Work

**Source repo:** `sjordan0228/hivesense` (SwiftUI + SwiftData, iOS 17+)
**Hardware spec:** `sjordan0228/hivesense-monitor/README.md` Section 8
**Design specs:** `sjordan0228/hivesense-monitor/docs/superpowers/specs/`

---

## What Exists Already

The app has Yard/Hive models, NFC, voice inspections (WhisperKit), inspection forms, subscription tiers (StoreKit 2), photo capture, and reports. No sensor data layer yet.

---

## What Needs to Be Built

### 1. SensorReading SwiftData Model

New `@Model` class linked to existing `Hive` model.

```swift
@Model
final class SensorReading {
    var id: UUID
    var timestamp: Date
    var weightKg: Double?
    var tempInternal: Double?
    var tempExternal: Double?
    var humidityInternal: Double?
    var humidityExternal: Double?
    var beesIn: Int?
    var beesOut: Int?
    var beesActivity: Int?
    var batteryPercent: Int?
    var source: ReadingSource  // .ble or .mqtt
    var hive: Hive?
}

enum ReadingSource: UInt8, Codable, CaseIterable {
    case ble = 0
    case mqtt = 1
}
```

Add to `Hive` model:
```swift
@Relationship(deleteRule: .cascade, inverse: \SensorReading.hive)
var sensorReadings: [SensorReading]

var sensorMacAddress: String?  // BLE MAC of paired ESP32
var lastSensorSync: Date?
```

### 2. BLE Integration (CoreBluetooth)

**New file:** `HiveSenseBLEService.swift`

- Scan for peripherals advertising HiveSense service UUID (`4E6F7200-7468-6976-6553-656E73650000`)
- Discover hive nodes, read hive ID characteristic
- Download sensor log via notify characteristic (array of HivePayload structs)
- Write clear command (0x01) after successful download
- Pair flow: write hive ID to ESP32's Hive ID characteristic

**BLE Characteristics (match firmware):**
| UUID suffix | Name | Properties |
|---|---|---|
| 0001 | Sensor Log | Read, Notify |
| 0002 | Reading Count | Read |
| 0003 | Hive ID | Read, Write |
| 0004 | Clear Log | Write |

**Key:** The ESP32 sends raw packed C structs over BLE. The iOS app must deserialize `HivePayload` (48 bytes packed) using `withUnsafeBytes` or a manual byte parser. Field order: version(1), hive_id(16), timestamp(4), weight_kg(4), temp_internal(4), temp_external(4), humidity_internal(4), humidity_external(4), bees_in(2), bees_out(2), bees_activity(2), battery_pct(1), rssi(1).

### 3. MQTT Integration (CocoaMQTT)

**New file:** `MQTTService.swift` (@MainActor ObservableObject)

- SPM dependency: `https://github.com/emqx/CocoaMQTT.git`
- Connect to HiveMQ Cloud via TLS (port 8883)
- Subscribe to `hivesense/hive/+/#`
- Parse incoming messages → create `SensorReading` entries
- Resolve hive ID from topic path (e.g., `hivesense/hive/HIVE-001/weight` → "HIVE-001")
- Reconnect on disconnect

**MQTT Topics published by collector:**
| Topic | Payload | Swift type |
|---|---|---|
| `hivesense/hive/{id}/weight` | kg | Double |
| `hivesense/hive/{id}/temp/internal` | C | Double |
| `hivesense/hive/{id}/temp/external` | C | Double |
| `hivesense/hive/{id}/humidity/internal` | %RH | Double |
| `hivesense/hive/{id}/humidity/external` | %RH | Double |
| `hivesense/hive/{id}/bees/in` | count | Int |
| `hivesense/hive/{id}/bees/out` | count | Int |
| `hivesense/hive/{id}/bees/activity` | count | Int |
| `hivesense/hive/{id}/battery` | % | Int |
| `hivesense/hive/{id}/rssi` | dBm | Int |

**Config UI:** New "Cloud Monitoring" section in Settings with broker host, port, username, password, enable toggle, and "Test Connection" button.

### 4. Sensors Tab (HiveDetailScreen)

Add 6th tab "Sensors" to the existing segmented picker.

**Layout:**
- Weight trend — 7-day Swift Charts line chart, current value + weekly change
- Temperature & humidity — internal vs external side-by-side cards
- Bee traffic — daily in/out/net/activity
- Battery bar + last sync timestamp

**Uses:** Swift Charts (built-in iOS 16+)

### 5. Sensor Alerts

**New file:** `SensorAlertService.swift`

Checks incoming readings against configurable thresholds:

| Alert | Trigger | Severity |
|---|---|---|
| Weight drop | > 3 kg in 1 hour | Critical (swarm) |
| Weight decline | > 1 kg/day sustained | Warning |
| Temp cold | Internal < 32C | Warning |
| Temp hot | Internal > 40C | Warning |
| Humidity high | Internal > 80% RH | Warning |
| No activity | 0 bees for 2+ hrs daytime | Critical |
| Battery low | < 20% | Info |
| Sensor offline | No data for 2+ hrs | Warning |

Triggers local notifications via `UNUserNotificationCenter`. Shows alert badges on Dashboard.

### 6. Dashboard Updates

- "Sensors" metric card showing active sensor node count
- Alert count includes sensor alerts
- "Needs Attention" includes sensor anomalies

### 7. BLE Sync UI

**New file:** `SensorSyncView.swift`

Yard visit flow:
1. User opens Yard Mode → BLE scan starts
2. Shows "N hives with sensor data"
3. "Sync Sensors" button → connects to each, downloads, creates SensorReading entries
4. Sends clear command to ESP32

**Pairing flow:**
1. YardDetailScreen → "Pair Sensor" on a hive
2. Scan for unpaired HiveSense BLE devices
3. Select by proximity/signal strength
4. Write hive ID to ESP32 characteristic
5. Store MAC on Hive model

### 8. OTA Trigger UI

**New section in Settings or Yard detail:**
- "Update Firmware" button
- Select release tag (could fetch from GitHub API)
- Pick target: specific hive or collector
- Publishes MQTT message to `hivesense/ota/start`:
  ```json
  {"hive_id": "HIVE-003", "tag": "v1.2.0", "target": "node"}
  ```

---

## Files to Create

| File | Purpose |
|---|---|
| `Models/SensorReading.swift` | SwiftData model |
| `Models/Enums/ReadingSource.swift` | BLE vs MQTT enum |
| `Services/HiveSenseBLEService.swift` | CoreBluetooth BLE client |
| `Services/MQTTService.swift` | CocoaMQTT wrapper |
| `Services/SensorAlertService.swift` | Threshold checks + notifications |
| `Features/Hive/SensorsTab.swift` | Charts and sensor display |
| `Features/Settings/CloudMonitoringView.swift` | MQTT config UI |
| `Features/Yard/SensorSyncView.swift` | BLE sync UI |

## SPM Dependencies to Add

| Package | URL |
|---|---|
| CocoaMQTT | `https://github.com/emqx/CocoaMQTT.git` |

CoreBluetooth and Swift Charts are built-in frameworks.

---

## Suggested Build Order

1. SensorReading model + Hive relationship (foundation)
2. BLE service + sync UI (testable with hardware at yard)
3. MQTT service + Cloud Monitoring settings (testable with HiveMQ account)
4. Sensors tab with charts (display layer)
5. Alert service + dashboard updates
6. OTA trigger UI

Each step is independently testable and shippable.
