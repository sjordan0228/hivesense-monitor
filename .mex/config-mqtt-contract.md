# CombSense iOS ↔ Firmware Per-Hive Config Contract

> **Status:** Locked draft, 2026-05-03. iOS and firmware Claude sessions agreed on all decisions in §1–§3 and edge cases §i–§m below. This is the canonical contract for runtime feature flags and per-hive remote configuration. Firmware implementation tracks this doc; iOS implementation tracks this doc. **Do not change without bumping the version at the bottom and re-confirming both sides.**
>
> **Sister contract:** `/tmp/scale-mqtt-contract.md` covers `scale/cmd` / `scale/status` / `scale/config`. This contract is independent — config-write does NOT use the scale-side keep-alive mechanism (see §1 below).

---

## 1. Topics

`{deviceId}` = the value iOS stores in `Hive.sensorMacAddress`. Same convention as the scale contract — opaque string, no parsing, no length assumption.

| # | Topic | Direction | Retain | QoS | Purpose |
|---|---|---|---|---|---|
| 1.1 | `combsense/hive/{deviceId}/capabilities` | firmware → iOS | **true** | 0 | Sensor mix, board, fw version, last boot. iOS subscribes; firmware re-publishes on boot + on feature-flag changes. |
| 1.2 | `combsense/hive/{deviceId}/config` | iOS → firmware | **true** | 0 | iOS writes feature flags + other config keys. Firmware clears the retain after applying. |
| 1.3 | `combsense/hive/{deviceId}/config/ack` | firmware → iOS | false | 0 | Per-key validation result for each `config` write. iOS waits on this for "Updated" UI confirmation. |
| 1.4 | `combsense/hive/{deviceId}/config/get` | iOS → firmware | false | 0 | iOS query for current config values. Empty payload = "all keys"; or `{"keys":[...]}` for a subset. |
| 1.5 | `combsense/hive/{deviceId}/config/state` | firmware → iOS | false | 0 | Response to `config/get`. Excludes policy-blocked keys (`wifi_pass`, `mqtt_pass`). |

iOS subscribes via the existing wildcard `combsense/hive/+/#`, so no new subscription plumbing is needed. All five topics are already covered.

**About QoS:** firmware uses PubSubClient (QoS 0 only). iOS publishes at QoS 1 by default; broker downgrades to QoS 0 to match the firmware subscription. Acceptable per the existing scale-side contract decision in `/tmp/scale-mqtt-contract.md` §6.

---

## 2. Wake-coordination decision

**No keep-alive plumbing for config writes.** iOS publishes a retained `config` payload, the firmware applies it on its next normal wake cycle (≤5 min), then publishes ack + cleared retain + refreshed capabilities. iOS UI shows an "Updating sensor… up to 5 minutes" spinner waiting for ack.

Rationale:
- Config changes are rare (Settings save, not per-tap)
- 5-min worst case is acceptable for a Settings save
- Decouples this contract from the calibration-keep-alive contract — they evolve independently
- Firmware: zero new keep-alive plumbing; reuses existing remote-config infrastructure (commit 409ce26 on the firmware side)

If a tag is offline when iOS publishes, the retained message stays on the broker until the tag wakes — that's the desired behavior (config eventually applies).

### The retained-clear sequence (CRITICAL for iOS)

After applying, the firmware **immediately publishes an empty payload to the same `config` topic with retain=true** to clear the retained message on the broker. Without this, the retained config would re-trigger on every subsequent boot, making config writes "sticky" and confusing iOS-side UX.

Full happy-path sequence:

```
1.  iOS:      publish combsense/hive/{id}/config           retain=true   payload={"feat_scale":1,"sample_int":300}
              ↓
2.  …firmware deep-sleeping. Wakes within ≤5 min on RTC alarm…
              ↓
3.  firmware: apply to NVS (atomic per-key)
              ↓
4.  firmware: publish combsense/hive/{id}/config           retain=true   payload=""   (clears retain)
              ↓
5.  firmware: publish combsense/hive/{id}/config/ack       retain=false  payload={"event":"config_applied","results":{"feat_scale":"ok","sample_int":"ok"},"ts":"..."}
              ↓
6.  firmware: publish combsense/hive/{id}/capabilities     retain=true   payload={...new state...}     (only if a feat_* key changed)
              ↓
7.  firmware: continues normal wake cycle (publish reading, deep_sleep)
```

**iOS implications of the clear:**

- After a successful config publish, the `config` topic will appear empty on the broker. **Don't treat "no retained config" as an error or absence** — it just means the last config was applied and cleared.
- iOS uses the **`config/ack`** as the source of truth that the change was applied, NOT the presence/absence of the retained message.
- If iOS subscribes to `config` topic to track outstanding writes, the empty-payload publish will arrive — handle as "in-flight write completed" (or just ignore).

---

## 3. Capabilities (`combsense/hive/{deviceId}/capabilities`)

**Direction:** firmware → iOS
**Retain:** `true`
**QoS:** 0
**Subscribed by iOS via existing `combsense/hive/+/#` wildcard.**

### 3.1 Payload schema

```json
{
  "feat_ds18b20": 1,
  "feat_sht31":   0,
  "feat_scale":   1,
  "feat_mic":     0,
  "hw_board":     "xiao-c6",
  "fw_version":   "abc1234",
  "last_boot_ts": "2026-05-03T19:42:00Z",
  "ts":           "2026-05-03T20:00:00Z"
}
```

| Field | Type | Notes |
|---|---|---|
| `feat_*` | int (0 or 1) | Each present `feat_*` key reflects the current NVS value (or compile-time default if NVS is empty). Unknown `feat_*` keys NOT in the published payload mean "this firmware doesn't implement that feature." |
| `hw_board` | string | Free-form board identifier. iOS doesn't branch on this for v1; useful for diagnostics and future "incompatible feature" warnings. Examples: `"xiao-c6"`, `"feather-s3"`. |
| `fw_version` | string | Same git short-SHA already in the `reading` payload. Harmless duplication — lets the Settings screen show fw version without joining tables. |
| `last_boot_ts` | RFC3339 string (UTC `Z`, no fractional) | Set on cold boot (after NTP sync). iOS uses this to display "Last boot: 3h ago" in Settings, indicating tag liveness. If NTP wasn't synced at boot, firmware emits `"1970-01-01T00:00:00Z"`; iOS displays this as "unknown". (Format chosen for consistency with `ts` everywhere else in this contract.) |
| `ts` | RFC3339 string (UTC `Z`, no fractional) | When firmware sent this capabilities message. |

### 3.2 Re-publish triggers

Firmware re-publishes capabilities whenever ANY of these occur:

1. **Cold boot.** Handles fresh deploy, OTA reboot, brownout recovery. Apply any retained config FIRST (per §10k) so capabilities reflects post-apply state.
2. **After applying a config that changes any `feat_*` flag** (success path).
3. **After applying a config that REJECTS any `feat_*` change** (validation failure). This keeps iOS's cached capabilities authoritative without needing iOS to infer state from per-key ack results.

**NOT triggered by:**
- Configs that only touch non-feature keys (e.g., `sample_int` change → no capabilities re-publish).
- Idempotent writes to `feat_*` keys (i.e., the value didn't actually change → no re-publish, ack returns `"unchanged"` per §6).

### 3.3 iOS handling

- Subscribe via existing wildcard.
- Decode into a Swift type (proposal in §11) and mirror to SwiftData on the matching `Hive` row.
- Use cached values to drive UI gating (calibration wizard hidden if `feat_scale=0`, etc.).
- If iOS opens Settings while the tag is offline, show last-known capabilities + a "last seen" hint computed from `last_boot_ts` or the existing `Hive.lastSensorSync`.
- On a brand-new tag with no cached capabilities, default to **"hide all sensor-specific UI"** (safe default) until the first capabilities arrive.

---

## 4. Config write (`combsense/hive/{deviceId}/config`)

**Direction:** iOS → firmware
**Retain:** `true`
**QoS:** 1 (iOS-side intent; broker downgrades to 0 for firmware delivery)

### 4.1 Payload schema (flat — no `"set":` wrapper)

```json
{"feat_scale": 1, "sample_int": 300}
```

| Constraint | Detail |
|---|---|
| Format | Flat JSON object. Keys at top level. Values are int / string / bool / double per the key's declared type. |
| Empty object | `{}` is invalid — firmware will reject as `"empty"`. Don't publish empty payloads as a probe (use `config/get` for that). |
| Empty payload | Empty string (`""`) is the **clear-retain signal** firmware uses (§2). iOS should never publish an empty string. |
| Unknown keys | Firmware returns `"unknown_key"` per-key in the ack. The set keys are still applied. |

### 4.2 Allowed keys (this iteration)

| Key | Type | Default | Notes |
|---|---|---|---|
| `feat_ds18b20` | int (0/1) | per compile-time `DEFAULT_FEAT_DS18B20` | Mutually exclusive with `feat_sht31`. |
| `feat_sht31` | int (0/1) | per compile-time `DEFAULT_FEAT_SHT31` (typically 0) | Mutually exclusive with `feat_ds18b20`. |
| `feat_scale` | int (0/1) | per compile-time `DEFAULT_FEAT_SCALE` (typically 0) | Independent. |
| `feat_mic` | int (0/1) | per compile-time `DEFAULT_FEAT_MIC` (typically 0) | Independent. |
| `sample_int` | int (sec) | (firmware default — typically 300) | Existing key, unchanged. |
| `upload_every` | int | (firmware default) | Existing key, unchanged. |
| `tag_name` | string | (firmware default) | Existing key, unchanged. |
| `ota_host` | string (URL) | (firmware default) | Existing key, unchanged. |

### 4.3 Excluded-by-policy keys (firmware will reject)

These would brick a tag if mistyped remotely; firmware rejects with `"excluded:<name>"`:

- `wifi_ssid`
- `wifi_pass`
- `mqtt_pass`

iOS should never expose UI to set these via the config topic.

### 4.4 Future `feat_*` keys (forward compat)

When firmware adds new features, they follow the `feat_<name>` convention:
- `feat_co2`
- `feat_ir_counter`
- `feat_pir_motion`

iOS should treat any `feat_*` key it doesn't know about as "unknown feature, ignore" — don't show UI for it, don't fail. Capabilities tells iOS what features the tag *has*; new tags can have features iOS hasn't shipped UI for yet without breaking.

---

## 5. Config ack (`combsense/hive/{deviceId}/config/ack`)

**Direction:** firmware → iOS
**Retain:** `false`
**QoS:** 0

### 5.1 Payload schema (rich, per-key results)

```json
{
  "event":   "config_applied",
  "results": {
    "feat_scale":  "ok",
    "feat_sht31":  "conflict:feat_ds18b20",
    "ota_host":    "invalid:not_a_url",
    "sample_int":  "unchanged",
    "wifi_pass":   "excluded:wifi_pass"
  },
  "ts": "2026-05-03T20:01:14Z"
}
```

| Field | Type | Notes |
|---|---|---|
| `event` | string | Always `"config_applied"` (single event variant for this topic). |
| `results` | object | One entry per key in the original write. Value is a category-prefixed string per §6. iOS can branch on the category prefix and show the detail verbatim. |
| `ts` | RFC3339 string | Same convention as scale events — UTC `Z`, fractional optional. |

### 5.2 iOS handling

- iOS publishes `config` (retained) → starts a 6-minute timer + spinner.
- iOS subscribes (via existing wildcard) for `config/ack`.
- On `config/ack` arrival, iOS matches the `results` keys against what it published. If keys match, dismiss spinner.
- For each key: branch on category (§6) and surface in UI:
  - `ok` / `unchanged`: green "✓ Updated" or "No change"
  - `conflict:X`, `invalid:Y`, `unknown_key`, `excluded:Z`: red inline error next to the field
- After ack arrives, iOS does NOT need to re-publish a corrective config — the user must fix the value and tap Save again.
- If 6 min elapse without ack, show "Tag offline — try again". (Same UX as the scale wizard's wake timeout.)

---

## 6. Error code namespace (used in `config/ack` results)

`category:detail` format. iOS branches on category, displays detail.

| Category | Example | Meaning |
|---|---|---|
| `ok` | `"ok"` | Value applied, NVS rewritten. |
| `unchanged` | `"unchanged"` | Value matches current — NVS not rewritten. iOS treats as a no-op success; firmware does NOT re-publish capabilities (no flag actually changed). |
| `unknown_key` | `"unknown_key"` | Key not in firmware's allowed list. iOS shouldn't surface this for first-class fields; it indicates an iOS/firmware version skew. |
| `excluded` | `"excluded:wifi_pass"` | Key blocked by policy. iOS UI must not even allow attempting this. |
| `invalid` | `"invalid:not_a_url"`, `"invalid:out_of_range"`, `"invalid:type"` | Type or range validation failed. Detail tells iOS what to display ("not a URL", "out of range", etc.). |
| `conflict` | `"conflict:feat_ds18b20"` | Mutually exclusive with another already-set key. Detail names the key it conflicts with. |

iOS-side mapping suggestion:

```swift
enum ConfigResultCategory: String {
    case ok, unchanged, unknown_key, excluded, invalid, conflict
}

struct ConfigResult {
    let category: ConfigResultCategory
    let detail: String?    // text after the colon, if any
}
```

Future categories may be added by the firmware. iOS should treat unknown categories as `invalid` (safest red error), and log the raw string.

---

## 7. Config get / state (`combsense/hive/{deviceId}/config/get` → `.../config/state`)

**Direction:** iOS → firmware (request) → firmware → iOS (response)
**Retain:** false on both
**QoS:** 0

### 7.1 Request payload

Empty (request all keys):
```
""
```

Or subset:
```json
{"keys":["sample_int","feat_scale"]}
```

### 7.2 Response payload (`config/state`)

```json
{
  "event":  "config_state",
  "values": {
    "feat_ds18b20": 1,
    "feat_scale":   0,
    "sample_int":   300,
    "tag_name":     "yard"
  },
  "ts": "2026-05-03T20:01:14Z"
}
```

| Field | Notes |
|---|---|
| `event` | Always `"config_state"`. |
| `values` | Current NVS values (or compile-time defaults if unset). Excluded-by-policy keys (`wifi_pass`, `mqtt_pass`) are NEVER returned — firmware silently omits them even if explicitly requested in the `keys` array. |
| `ts` | RFC3339, same convention. |

### 7.3 When iOS uses this

- **Settings screen open**: iOS publishes `config/get` (empty) immediately on screen appear, populates form fields from the response. This avoids the "stale capabilities cache" problem where iOS shows defaults that don't match the tag's actual NVS state.
- iOS waits up to 5 min (same wake-cycle budget) for the response; while waiting, shows a "Loading current settings…" placeholder instead of pre-filling with potentially-stale values.
- If response doesn't arrive, fall back to last-known capabilities + show a "values may be stale" warning.

---

## 8. Timing & latency

| Phase | Latency | Notes |
|---|---|---|
| iOS publishes `config` → firmware applies → ack | **≤5 min worst case** | Firmware is asleep up to 5 min. No keep-alive wake-up. |
| iOS publishes `config/get` → firmware response | Same ≤5 min worst case | Same wake constraint. |
| iOS UI spinner timeout | **6 min** | Matches scale wizard. iOS shows "Tag offline — try again." |
| Capabilities re-publish after config-apply | Within same wake cycle (firmware-side ms) | iOS gets the refreshed capabilities essentially at the same instant as the ack. |

iOS does NOT retry config writes automatically. If 6 min elapses with no ack, the user must tap Save again (which re-publishes the retained config — replacing the previous one if the broker still holds it).

---

## 9. Idempotency

Firmware is idempotent on duplicate config writes:

- First publish of `{"feat_scale":1}` when current value is 0: NVS rewritten, capabilities re-published, ack `"feat_scale":"ok"`.
- Second publish of the same `{"feat_scale":1}` (no actual change): NVS NOT rewritten, capabilities NOT re-published, ack `"feat_scale":"unchanged"`.

iOS does NOT need to dedupe its own publishes. Hitting Save twice on the same form is safe.

---

## 10. Edge cases

### 10.a Concurrent extended-awake (scale wizard) + config write

If iOS publishes a config change while the firmware is in extended-awake mode for a calibration session, firmware applies the config write immediately (its MQTT subscribe is active), publishes ack + capabilities. The calibration session is unaffected — the two paths are independent.

**iOS-side recommendation:** the SwiftUI navigation stack already prevents being in Settings AND the calibration wizard at the same time (one fullScreenCover at a time per hive). If we ever enable a side panel or split view, this needs revisiting.

### 10.b Boot ordering — apply BEFORE publishing capabilities AND BEFORE sampling

On cold boot, firmware:
1. Reads NVS
2. Connects MQTT, subscribes
3. Waits 1.5s for retained `scale/config` (per scale contract) AND retained `config` (this contract) to drain
4. Applies any retained config (writes NVS, clears retain, publishes ack)
5. **THEN** samples sensors per the (possibly newly-updated) `feat_*` flags — `reading` payload reflects post-apply state
6. **THEN** publishes capabilities reflecting post-apply state
7. Publishes `reading`
8. (If extended-awake from `scale/config`, enter the wizard loop; else deep_sleep)

This ordering means iOS never sees a transient mismatch between `reading.w` and `capabilities.feat_scale` — both reflect the same post-config-apply state on the same wake. Cost: zero extra time on a normal wake (the 1.5s retained-config drain window is shared with the existing `scale/config` wait), one wake of "right" data instead of one wake of stale-then-self-correct.

### 10.c New tag (first cold boot, no NVS values)

Firmware uses compile-time defaults from `config.h`:

```cpp
#ifndef DEFAULT_FEAT_DS18B20
#define DEFAULT_FEAT_DS18B20 1
#endif
#ifndef DEFAULT_FEAT_SCALE
#define DEFAULT_FEAT_SCALE 0
#endif
// etc.
```

Capabilities reflects these defaults. iOS sees a sensible initial state without needing any config writes.

### 10.d Existing tag (old firmware, no `feat_*` in NVS, upgrades to new firmware)

Firmware sees no NVS keys → uses compile-time defaults derived from the tag's current OTA variant. E.g., a `xiao-c6-ds18b20` tag boots with `feat_ds18b20=1, feat_scale=0, feat_mic=0`. Capabilities reflects this. iOS day-1 of the rollout sees the right capabilities for every existing tag without any migration step.

### 10.e Rate limiting (iOS-side)

iOS Settings "Save" button rate-limited to **once per 30 seconds per hive**. Firmware doesn't strictly need this (one wake at a time), but it prevents UX confusion (mashing Save → multiple in-flight publishes → confusing ack stream).

---

## 11. Swift type definitions (proposed)

These are recommendations for the iOS side; firmware doesn't read this section.

### 11.1 `HiveCapabilities.swift`

```swift
import Foundation

/// Decoded from `combsense/hive/{deviceId}/capabilities` (retained=true).
/// Mirror to SwiftData on the matching Hive row for offline use.
struct HiveCapabilities: Codable, Equatable {
    let featDS18B20: Bool       // feat_ds18b20 (1 → true)
    let featSHT31:   Bool
    let featScale:   Bool
    let featMic:     Bool
    let hwBoard:     String     // "xiao-c6", "feather-s3", ...
    let fwVersion:   String     // git short SHA
    let lastBootTs:  Date?      // nil if firmware emitted "1970-01-01T00:00:00Z" (NTP not yet synced)
    let ts:          Date

    private enum CodingKeys: String, CodingKey {
        case featDS18B20 = "feat_ds18b20"
        case featSHT31   = "feat_sht31"
        case featScale   = "feat_scale"
        case featMic     = "feat_mic"
        case hwBoard     = "hw_board"
        case fwVersion   = "fw_version"
        case lastBootTs  = "last_boot_ts"
        case ts
    }

    // Custom decoder: int → Bool, epoch sec → Date, string → Date.
    // ...
}
```

### 11.2 `HiveConfigUpdate.swift`

```swift
import Foundation

/// Encodable struct for the `combsense/hive/{deviceId}/config` publish.
/// Flat JSON, only the fields iOS wants to change. Firmware ignores
/// any keys not set; encoder skips nil fields.
struct HiveConfigUpdate: Encodable, Equatable {
    var featDS18B20: Bool?
    var featSHT31:   Bool?
    var featScale:   Bool?
    var featMic:     Bool?
    var sampleInt:   Int?
    var uploadEvery: Int?
    var tagName:     String?
    var otaHost:     String?

    private enum CodingKeys: String, CodingKey {
        case featDS18B20 = "feat_ds18b20"
        case featSHT31   = "feat_sht31"
        case featScale   = "feat_scale"
        case featMic     = "feat_mic"
        case sampleInt   = "sample_int"
        case uploadEvery = "upload_every"
        case tagName     = "tag_name"
        case otaHost     = "ota_host"
    }

    /// True if at least one field is set (nothing to publish if not).
    var isEmpty: Bool {
        featDS18B20 == nil && featSHT31 == nil && featScale == nil &&
        featMic == nil && sampleInt == nil && uploadEvery == nil &&
        tagName == nil && otaHost == nil
    }
}
```

### 11.3 `ConfigAckEvent.swift`

```swift
import Foundation

/// Decoded from `combsense/hive/{deviceId}/config/ack`.
struct ConfigAckEvent: Decodable, Equatable {
    let event:   String                       // always "config_applied"
    let results: [String: ConfigResult]
    let ts:      Date
}

struct ConfigResult: Equatable {
    enum Category: String {
        case ok, unchanged, unknown_key, excluded, invalid, conflict
        case unknown   // any category the iOS app version doesn't recognize
    }
    let category: Category
    let detail:   String?     // text after the first ":"
    let raw:      String      // original string for logs

    init(_ s: String) {
        let parts = s.split(separator: ":", maxSplits: 1).map(String.init)
        let cat = parts.first ?? ""
        self.category = Category(rawValue: cat) ?? .unknown
        self.detail = parts.count > 1 ? parts[1] : nil
        self.raw = s
    }
}

// ConfigResult Decodable impl: decodes from String, parses via init(_ s:).
```

### 11.4 `ConfigState.swift`

```swift
import Foundation

/// Decoded from `combsense/hive/{deviceId}/config/state`.
/// Used by Settings screen to populate form fields with actual NVS values.
struct ConfigState: Decodable, Equatable {
    let event:  String                 // always "config_state"
    let values: [String: AnyCodable]   // type-erased — int / string / bool
    let ts:     Date
}
```

### 11.5 `MQTTService` extensions

```swift
extension MQTTService {
    /// Publish a config update. Retained=true, QoS 1.
    func publish(configUpdate: HiveConfigUpdate, forHiveId hiveId: String) throws { ... }

    /// Request current config values. Empty payload = all keys; pass `keys`
    /// for a subset.
    func publishConfigGet(forHiveId hiveId: String, keys: [String]? = nil) throws { ... }

    /// (Capabilities is just decoded inline in handleMessage — no helper needed.)
}
```

---

## 12. Real example payloads

### 12.1 capabilities

```json
{"feat_ds18b20":1,"feat_sht31":0,"feat_scale":1,"feat_mic":0,"hw_board":"xiao-c6","fw_version":"abc1234","last_boot_ts":"2026-05-03T19:42:00Z","ts":"2026-05-03T20:00:00Z"}
```

### 12.2 config (iOS publishes — single feature toggle)

```json
{"feat_scale":1}
```

### 12.3 config (iOS publishes — multiple keys including a sensor swap)

```json
{"feat_ds18b20":0,"feat_sht31":1,"sample_int":600,"tag_name":"hive-1"}
```

### 12.4 config (clear retain — firmware-only, NEVER iOS)

```
""
```

### 12.5 config/ack (mixed success/failure)

```json
{"event":"config_applied","results":{"feat_scale":"ok","feat_sht31":"conflict:feat_ds18b20","ota_host":"invalid:not_a_url"},"ts":"2026-05-03T20:01:14Z"}
```

### 12.6 config/get (request all)

```
""
```

### 12.7 config/get (request subset)

```json
{"keys":["sample_int","feat_scale","tag_name"]}
```

### 12.8 config/state

```json
{"event":"config_state","values":{"feat_ds18b20":1,"feat_scale":0,"sample_int":300,"tag_name":"yard","upload_every":1,"ota_host":"http://192.168.1.16:8080","hw_board":"xiao-c6","fw_version":"abc1234"},"ts":"2026-05-03T20:01:14Z"}
```

### 12.9 Full round-trip (Settings save with one rejected key)

```
# User toggles feat_scale on, also tries to enable feat_sht31 (which conflicts with feat_ds18b20).
# iOS:
combsense/hive/c513131c/config           ←  {"feat_scale":1,"feat_sht31":1}     (retain=true)

# …firmware deep-sleeping. Wakes ~3 min later…

# Firmware applies:
#   feat_scale=1: NVS write OK
#   feat_sht31=1: REJECT (conflicts with feat_ds18b20=1)

# Firmware:
combsense/hive/c513131c/config           →  ""                                   (retain=true, clears retain)
combsense/hive/c513131c/config/ack       →  {"event":"config_applied","results":{"feat_scale":"ok","feat_sht31":"conflict:feat_ds18b20"},"ts":"2026-05-03T20:01:14Z"}
combsense/hive/c513131c/capabilities     →  {"feat_ds18b20":1,"feat_sht31":0,"feat_scale":1,...,"ts":"..."}     (retain=true; feat_scale changed → re-publish)

# iOS dismisses spinner, shows green check next to "Weight" toggle, red error
# next to "Temperature/Humidity" toggle reading "Conflicts with current
# temperature sensor — disable Temperature first."
```

---

## 13. iOS lifecycle responsibilities (summary)

### On app start / hive added
- Subscribe via existing wildcard (already happens).
- When a `capabilities` retained message arrives for any hive, decode and mirror to SwiftData.

### On Hive Detail screen appear
- Read cached `HiveCapabilities` from SwiftData. Drive UI gating from this:
  - Calibration wizard menu item: hidden if `featScale == false`
  - Quick Re-tare button: hidden if `featScale == false`
  - Microphone tab/section: hidden if `featMic == false`
  - Temperature widget: always shown if EITHER `featDS18B20` OR `featSHT31` is true; hide if both false
  - Battery widget: always shown (baseline)

### On Settings screen appear
- Publish `config/get` (empty), wait for `config/state` response
- While waiting (≤5 min), show "Loading current settings…" placeholder
- On response, populate form fields from `values`
- On timeout, fall back to cached capabilities + show "values may be stale" warning

### On Settings save
- Build `HiveConfigUpdate` with only fields the user changed
- Validate mutual exclusion in UI before publishing (radio button enforces DS18B20 vs SHT31)
- Rate-limit Save button to once per 30 s per hive
- Publish to `config` topic, retain=true, QoS 1
- Show "Updating sensor… up to 5 minutes" spinner
- Subscribe-and-await `config/ack` matching the keys we published (use `ScaleEventCorrelator`-style first-match-per-hive — or reuse the same correlator if generalized)
- On ack: parse per-key results, show inline ✓ or red errors
- On 6-min timeout: show "Tag offline — try again"
- Capabilities will re-arrive automatically (firmware re-publishes if any feat_* changed); SwiftData mirror updates; UI gating refreshes reactively

### On app crash mid-write
- Retained `config` stays on broker. Firmware applies on next wake. iOS on relaunch sees the retain cleared (via the new empty publish) once the apply completes. Net effect: write succeeds eventually.

---

## 14. Open / future

- **`feat_*` capability discovery for unknown features.** When firmware adds `feat_co2`, an iOS app shipped before that feature won't show a UI toggle for it. Capabilities will still include the key, just iOS ignores it. Forward-compat is fine.
- **Full hardware report** in capabilities (pin assignments, NVS partition size, free heap). Defer until a debugging use case appears.
- **Bulk config import/export.** Not needed for v1; if it becomes useful, layer on top of `config/get` + `config`.
- **Per-feature sub-config.** E.g., scale-specific calibration intervals, mic-specific gain. For v1 the only feature with config is the scale via the existing `scale/cmd` contract; mic/SHT31 don't have user-facing settings yet.

---

## 15. Versioning

**Contract version:** 1.1
**Locked:** 2026-05-03 (iOS + firmware Claude sessions agreed on §1–§3 and edge cases §i–§m; v1.1 clarified `last_boot_ts` as RFC3339 string and §10.b boot ordering as sample-AFTER-config-apply)
**iOS PR-A target:** capabilities subscription + UI gating (read path only)
**iOS PR-B target:** Settings screen + write path + ack handling
**Firmware PR target:** runtime feature flags + capabilities publish + config_parser extension + rich ack + config/get → config/state

Future revisions: bump version, add changelog, re-confirm with both sides before changing wire format.
