# sensor-tag-wifi: fleet visibility & battery telemetry — Design

**Date:** 2026-04-25
**Branch:** `dev`
**Scope:** Add per-reading firmware version, raw battery voltage, and WiFi RSSI to the MQTT payload. Defer heartbeat work to a tracked issue.

## Goal

Every reading published to `combsense/hive/<id>/reading` self-identifies which firmware build produced it, what the cell voltage was at sample time, and what the WiFi signal looked like at publish time. This makes the fleet diagnosable from Influx alone — no need to cross-reference deploy logs to know what version a tag is running, and no need to trust the SOC curve when raw mV is available.

## Out of scope

- **Heartbeat / read-failure publishing.** Decision: defer (option C from brainstorming). Tracked as a GitHub issue containing the A/B/C trade-off analysis. Revisit once fleet >3 tags.
- iOS app changes. The Influx schema additions are non-breaking; existing fields are untouched.
- Grafana panel updates. Additive only — operator can wire the new fields when wanted.

## Payload shape

```json
{"id":"c5fffe12","v":"5423c04","t":1745520000,"t1":21.50,"t2":22.10,"vbat_mV":3987,"rssi":-58,"b":78}
```

**Field order:**
- `id`, `v` — identity prefix (who said this, what build)
- `t` — timestamp
- `t1`, `t2`, optional `h1`, `h2` — sensor channels. Existing NaN/null and humidity-omit semantics preserved.
- `vbat_mV`, `rssi`, `b` — diagnostics suffix

**Always-emit semantics** (per option (a) from brainstorming):
- `v` — always emitted. Falls back to literal `"unknown"` when `git describe` fails (existing [get_version.py](firmware/sensor-tag-wifi/get_version.py) behaviour).
- `vbat_mV` — always emitted, even if 0. Mirrors the existing `b=0` USB-bench convention; downstream queries can filter `WHERE vbat_mV > 100`.
- `rssi` — always emitted. Post-connect read is always valid since publish only happens after a successful WiFi connect.

**Buffer headroom:** worst-case payload (`v0.1.2-12-g5423c04-dirty`, both humidities present, vbat_mV=4-digit, rssi=-3-digit) ≈ 135 bytes. `PAYLOAD_MAX_LEN = 160` ([include/config.h:84](firmware/sensor-tag-wifi/include/config.h#L84)) holds.

## Field semantics: per-sample vs per-publish

`vbat_mV` is a **per-sample** measurement — read at sample time alongside temperatures, persists in the RTC ring buffer across deep-sleep cycles. It belongs in `Reading`.

`rssi` is a **per-publish** measurement — read once after WiFi connect, applies to all readings drained in that publish session. Storing it in `Reading` would be misleading (buffered readings get the current RSSI, not the RSSI at their sample time). It is passed as a parameter to `Payload::serialize`.

`v` is a **compile-time constant** — same for every publish from a given build. Passed as a parameter (rather than referencing the macro inside `payload.cpp`) so unit tests can supply arbitrary version strings without rebuilding.

## Code changes

### [include/reading.h](firmware/sensor-tag-wifi/include/reading.h)
Add one field. POD remains RTC-safe:
```cpp
struct Reading {
    uint32_t timestamp;
    float    temp1;
    float    temp2;
    float    humidity1;
    float    humidity2;
    uint16_t vbat_mV;     // NEW — raw battery voltage at sample time
    uint8_t  battery_pct;
};
```

### [src/battery.h](firmware/sensor-tag-wifi/src/battery.h) / [src/battery.cpp](firmware/sensor-tag-wifi/src/battery.cpp)
Refactor to expose the underlying mV value, single ADC sweep:
```cpp
namespace Battery {
    uint16_t readMillivolts();                       // ADC sweep + divider ratio
    uint8_t  percentFromMillivolts(uint16_t mV);     // pure conversion
    uint8_t  readPercent();                           // composition (kept as convenience)
}
```
Caller in `sampleAndEnqueue()` does one sweep, derives both:
```cpp
r.vbat_mV     = Battery::readMillivolts();
r.battery_pct = Battery::percentFromMillivolts(r.vbat_mV);
```

### [src/payload.h](firmware/sensor-tag-wifi/include/payload.h) / [src/payload.cpp](firmware/sensor-tag-wifi/src/payload.cpp)
Extend `serialize()` signature with two trailing parameters:
```cpp
int serialize(const char* deviceId,
              const char* fwVersion,
              int8_t      rssi,
              const Reading& r,
              char* buf, size_t bufLen);
```
Emission order matches the documented payload shape above. `vbat_mV` and `rssi` use integer formats (`%u` and `%d`).

### [src/mqtt_client.cpp](firmware/sensor-tag-wifi/src/mqtt_client.cpp)
- Capture `int8_t rssi = static_cast<int8_t>(WiFi.RSSI())` once, after connect, before drain.
- Pass `FIRMWARE_VERSION` and the captured rssi into `Payload::serialize()` for each buffered reading.

### [deploy/tsdb/telegraf-combsense.conf](deploy/tsdb/telegraf-combsense.conf)
Add three field blocks. All **fields** (not tags), all `optional = true` so older payloads from un-upgraded tags continue to parse:
```toml
[[inputs.mqtt_consumer.json_v2.field]]
  path = "v"
  rename = "fw_version"
  type = "string"
  optional = true
[[inputs.mqtt_consumer.json_v2.field]]
  path = "vbat_mV"
  type = "int"
  optional = true
[[inputs.mqtt_consumer.json_v2.field]]
  path = "rssi"
  type = "int"
  optional = true
```
String field for `fw_version` avoids Influx series-cardinality blow-up on every OTA (versus making it a tag).

## Testing

[firmware/sensor-tag-wifi/test/test_payload/test_payload.cpp](firmware/sensor-tag-wifi/test/test_payload/test_payload.cpp):

- **Update all 6 existing tests** to pass `fwVersion="abc1234"` and `rssi=-58` (or similar fixtures), and update expected JSON strings to include `"v":"abc1234"` after `id`, and `"vbat_mV":<n>,"rssi":<n>` before `b`.
- **Add 1 new test:** `test_serialize_with_unknown_version` — passes `fwVersion="unknown"`, asserts the literal appears in the payload.

Result: 7 payload tests (was 6). Total native test count goes from 30 → 31.

Verification command (per project instruction in user prompt):
```
pio test -e native -d firmware/sensor-tag-wifi
```

## Verification (pre-merge)

1. `pio test -e native -d firmware/sensor-tag-wifi` — all native tests pass.
2. `pio run -e xiao-c6-ds18b20` and `pio run -e xiao-c6-sht31` — both compile clean.
3. Bench: flash one tag, serial-monitor, confirm a publish line shows the new payload shape.
4. Telegraf: tail `/var/log/telegraf/telegraf.log` on combsense-tsdb after the bench tag publishes once — confirm no `json_v2` parse errors.
5. Influx sanity: `from(bucket:"combsense") |> range(start:-5m) |> filter(fn:(r) => r._field == "fw_version")` returns the build sha.

## Deferred follow-up

Open a GitHub issue **on dev branch tasks** titled "sensor-tag-wifi heartbeat (read-failure visibility)" containing the A/B/C trade-off analysis from brainstorming, with recommendation to revisit when fleet exceeds 3 tags.

## ROUTER update

After merge, append to `.mex/ROUTER.md`:
- Bullet under sensor-tag-wifi section: "Reading payload now self-identifies build (`v`) and includes raw battery mV + WiFi RSSI for diagnostics."
- Telegraf line: "fw_version, vbat_mV, rssi captured as Influx fields."
- Native test count: 30 → 31.
