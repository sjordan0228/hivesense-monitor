# Sensor History Service Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Tasks span three repos (combsense-monitor firmware, Proxmox host config, combsense iOS app) and an LXC host — not all steps are in this working directory. Paths are absolute where they cross systems.

**Goal:** Persist every sensor reading into a queryable time-series store so the iOS app and future Grafana dashboards can graph historical data, scaling to 50 hives.

**Architecture:** Mosquitto (existing, `192.168.1.82:1883`) → Telegraf MQTT input → InfluxDB 2.x. Grafana queries Influx for home dashboards; iOS app queries Influx's HTTP API (`/api/v2/query` with Flux) for in-app charts. All three services live in one native Debian 12 LXC on Proxmox — no Docker. Firmware begins emitting real epoch timestamps so offline-batched readings from the RTC ring buffer land at their true sense time rather than arrival time.

**Tech Stack:** InfluxDB 2.7 OSS, Telegraf 1.29+, Grafana 10.x, Mosquitto 2.x, ESP32 Arduino (existing firmware), Swift 5.9 + Swift Charts (existing iOS app), Flux query language.

---

## Scope notes

- **LXC host:** new Debian 12 LXC on Proxmox. Suggested hostname `combsense-tsdb`, static LAN IP (document in Task 1). Sized 2 vCPU / 2 GB RAM / 20 GB LVM volume — sufficient for 50 sensors at 5-min cadence for multi-year retention.
- **Single broker** confirmed — Telegraf subscribes to `192.168.1.82:1883` only. If per-yard brokers appear later, add a second `[[inputs.mqtt_consumer]]` stanza.
- **Influx auth model:** admin token for setup, an **operator token** for Telegraf (write-only to one bucket), a **read token** for the iOS app (read-only to all history buckets). Never reuse.
- **Backups:** LVM snapshot on the LXC volume + periodic `influx backup` cron to an offsite target — specified in Task 3.
- **iOS Settings** already has a Cloud Monitoring pane for MQTT — extend it, don't create a new pane.

---

## File/Component Structure

Across three systems:

**On the LXC (`combsense-tsdb`):**
- `/etc/telegraf/telegraf.conf` — overwritten with our MQTT-to-Influx config
- `/etc/telegraf/telegraf.d/combsense.conf` — our additions (kept out of the main file so apt upgrades don't clobber)
- `/etc/grafana/grafana.ini` — minor edits (bind address, admin creds)
- `/var/lib/influxdb2/` — Influx data root (default)
- `/var/backups/combsense-tsdb/` — backup target
- `/etc/cron.d/combsense-tsdb-backup` — backup schedule
- `/etc/systemd/system/combsense-backup.service` + `.timer` — systemd-native alternative to cron (use whichever fits; plan shows systemd)

**In `firmware/sensor-tag-wifi/`:**
- `src/main.cpp` — add NTP sync after WiFi connect, pass `time()` into payload
- `src/payload.cpp` — already emits `t`; change default from 0 to real epoch when sync'd
- `include/payload.h` — no change (field already exists)

**In iOS app (`sjordan0228/combsense`):**
- `CombSense/Services/HistoryService.swift` — **NEW.** Swift concurrency-based client for Influx `/api/v2/query`.
- `CombSense/Features/Hive/HiveHistoryView.swift` — **NEW.** Swift Charts view with range selector (24h / 7d / 30d / 1y).
- `CombSense/Features/Settings/CloudMonitoringView.swift` — extend with Influx host / org / bucket / token fields.
- `CombSense/Models/HistoryReading.swift` — **NEW.** Simple struct for chart consumption (not SwiftData — it's read-only projection).
- `CombSenseTests/Services/HistoryServiceTests.swift` — **NEW.** Mocks URLSession, asserts Flux query strings + response decoding.

---

## Task 1: Provision LXC + install Influx, Telegraf, Grafana

**Files/Targets:**
- Proxmox host (GUI or `pct` CLI)
- LXC container `combsense-tsdb` (Debian 12)

- [ ] **Step 1.1: Create the LXC on Proxmox**

From the Proxmox host shell or web UI:

```bash
# Using the tteck community helper (recommended — handles network + defaults)
bash -c "$(wget -qLO - https://github.com/tteck/Proxmox/raw/main/ct/debian.sh)"
```

Or manual:

```bash
pct create <ID> local:vztmpl/debian-12-standard_12.2-1_amd64.tar.zst \
  --hostname combsense-tsdb \
  --cores 2 --memory 2048 --swap 512 \
  --rootfs local-lvm:20 \
  --net0 name=eth0,bridge=vmbr0,ip=dhcp \
  --onboot 1 --features nesting=0
pct start <ID>
```

Verify: `pct status <ID>` → running; `pct exec <ID> -- ip -4 addr` → has a LAN IP. Write the IP down (referred to as `TSDB_IP` below). Consider assigning a DHCP reservation on your router so it's stable.

- [ ] **Step 1.2: Base OS prep**

```bash
pct exec <ID> -- bash -c "apt-get update && apt-get upgrade -y && apt-get install -y curl gnupg ca-certificates sudo"
```

Expected: apt completes without errors.

- [ ] **Step 1.3: Install InfluxDB 2.x from upstream repo**

```bash
pct exec <ID> -- bash -c '
curl -sL https://repos.influxdata.com/influxdata-archive.key | \
  gpg --dearmor -o /usr/share/keyrings/influxdata-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/influxdata-archive-keyring.gpg] https://repos.influxdata.com/debian stable main" \
  > /etc/apt/sources.list.d/influxdata.list
apt-get update
apt-get install -y influxdb2 influxdb2-cli
systemctl enable --now influxdb
'
```

Verify: `pct exec <ID> -- systemctl is-active influxdb` → `active`; `pct exec <ID> -- curl -s localhost:8086/health` → JSON with `"status":"pass"`.

- [ ] **Step 1.4: Bootstrap Influx — create org, bucket, admin token**

On the LXC (via `pct exec <ID> -- influx ...` or `pct enter <ID>` for an interactive shell):

```bash
influx setup \
  --username admin \
  --password '<strong-password-you-pick>' \
  --org combsense \
  --bucket combsense \
  --retention 30d \
  --force
```

Expected output: a table with `User`, `Organization`, `Bucket`, and `Token`. **Save the admin token** to a password manager — you'll need it to mint the Telegraf and iOS tokens.

- [ ] **Step 1.5: Mint a Telegraf write token and an iOS read token**

```bash
# Write token — scoped to the `combsense` bucket only
influx auth create \
  --org combsense \
  --description "telegraf-write" \
  --write-bucket $(influx bucket find -n combsense --hide-headers | awk '{print $1}')

# Read token — read-all for the org (app will query multiple buckets as we add them)
influx auth create \
  --org combsense \
  --description "ios-read" \
  --read-buckets --read-tasks --read-dashboards --read-telegrafs
```

Save both tokens. Each is printed on the `Token` line — capture now, you can't retrieve later.

- [ ] **Step 1.6: Install Telegraf**

```bash
pct exec <ID> -- bash -c '
apt-get install -y telegraf
systemctl enable telegraf
# Do not start yet — we need to write config first
'
```

- [ ] **Step 1.7: Install Grafana**

```bash
pct exec <ID> -- bash -c '
mkdir -p /etc/apt/keyrings
curl -fsSL https://apt.grafana.com/gpg.key | gpg --dearmor -o /etc/apt/keyrings/grafana.gpg
echo "deb [signed-by=/etc/apt/keyrings/grafana.gpg] https://apt.grafana.com stable main" \
  > /etc/apt/sources.list.d/grafana.list
apt-get update
apt-get install -y grafana
systemctl enable --now grafana-server
'
```

Verify: `pct exec <ID> -- systemctl is-active grafana-server` → `active`. Open `http://<TSDB_IP>:3000` in a browser, log in `admin` / `admin`, change the password on first prompt.

- [ ] **Step 1.8: Commit the LXC ID + IP to the repo**

Update `.mex/ROUTER.md` — add a line under reference infrastructure:

```
- combsense-tsdb LXC: <TSDB_IP>:8086 (Influx), :3000 (Grafana) — Proxmox LXC <ID>
```

Commit with message `chore(mex): note tsdb LXC location`.

---

## Task 2: Configure Telegraf — MQTT input → InfluxDB output

**Files:**
- Create: `/etc/telegraf/telegraf.d/combsense.conf` (on `combsense-tsdb`)
- Modify: none (leave stock `/etc/telegraf/telegraf.conf` alone)

**Why this file and not the main conf:** Telegraf loads every `.conf` under `telegraf.d/` automatically. Keeping our config in its own file means apt upgrades of the stock conf never clobber it.

- [ ] **Step 2.1: Write the Telegraf config**

On the LXC, `/etc/telegraf/telegraf.d/combsense.conf`:

```toml
[[inputs.mqtt_consumer]]
  servers = ["tcp://192.168.1.82:1883"]
  topics = ["combsense/hive/+/reading"]
  qos = 0
  connection_timeout = "30s"
  client_id = "telegraf-combsense-tsdb"
  username = "hivesense"
  password = "hivesense"

  data_format = "json_v2"

  [[inputs.mqtt_consumer.json_v2]]
    # Pull `id` out as a tag so we can filter by sensor in Flux queries.
    # Everything else becomes a field. We use firmware's `t` as the
    # timestamp when it's > 0 (real epoch); otherwise Telegraf stamps
    # with arrival time, which is what we want as a fallback.
    measurement_name = "sensor_reading"
    timestamp_path = "t"
    timestamp_format = "unix"

    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "id"
      rename = "sensor_id"

    [[inputs.mqtt_consumer.json_v2.field]]
      path = "t1"
      type = "float"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "t2"
      type = "float"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "b"
      type = "int"
      optional = true

[[outputs.influxdb_v2]]
  urls = ["http://127.0.0.1:8086"]
  token = "<telegraf-write-token-from-Task-1.5>"
  organization = "combsense"
  bucket = "combsense"
```

Replace `<telegraf-write-token-from-Task-1.5>` with the actual token. Lock the file: `chmod 640 /etc/telegraf/telegraf.d/combsense.conf && chown root:telegraf /etc/telegraf/telegraf.d/combsense.conf`.

- [ ] **Step 2.2: Test-run Telegraf without writing**

```bash
pct exec <ID> -- telegraf --config /etc/telegraf/telegraf.d/combsense.conf --test
```

Expected: after a few seconds, console prints one line per MQTT message parsed. If nothing arrives because no sensor is publishing yet, manually publish a test message from your dev machine:

```bash
mosquitto_pub -h 192.168.1.82 -u hivesense -P hivesense \
  -t combsense/hive/c5fffe12/reading \
  -m '{"id":"c5fffe12","t":0,"t1":22.0,"t2":null,"b":50}'
```

`--test` mode should then show a parsed point with tag `sensor_id=c5fffe12` and fields `t1=22`, `b=50`.

- [ ] **Step 2.3: Start Telegraf**

```bash
pct exec <ID> -- systemctl start telegraf
pct exec <ID> -- journalctl -u telegraf -n 40 --no-pager
```

Expected: journal shows `Successfully connected to tcp://192.168.1.82:1883` and no JSON parse errors. If you see parse errors, re-run `--test` (Step 2.2) with an actual published payload and compare field paths.

- [ ] **Step 2.4: Verify data lands in Influx**

From the LXC:

```bash
pct exec <ID> -- influx query 'from(bucket:"combsense") |> range(start:-15m) |> filter(fn:(r) => r._measurement == "sensor_reading") |> limit(n:5)'
```

Expected: a table with columns `_time`, `sensor_id`, `_field`, `_value`. If empty, check journalctl for Telegraf, then (cause order) MQTT connectivity → JSON parse → Influx write.

- [ ] **Step 2.5: Commit nothing** (Telegraf config is on the LXC, not in the repo)

But: paste the final `combsense.conf` into a new file `deploy/tsdb/telegraf-combsense.conf` in the repo for reference/backup. Commit with:

```bash
git add deploy/tsdb/telegraf-combsense.conf
git commit -m "docs(tsdb): snapshot telegraf config for combsense-tsdb LXC"
```

(Directory doesn't exist yet — create it; no CI hits this path.)

---

## Task 3: Retention + downsampling

**Why:** at 50 hives × 1 reading / 5 min × 4 fields/tag overhead, Influx stores ~60M points/year raw. Fine on disk but graph queries over a year of raw data get slow. Downsample to hourly and daily aggregates, and age out raw data after 30 days.

**Files:**
- Create: `deploy/tsdb/downsample-1h.flux` (in repo, for reference)
- Create: `deploy/tsdb/downsample-1d.flux` (in repo, for reference)
- Configure: Influx tasks (not in repo — configured via `influx task create`)

- [ ] **Step 3.1: Create two additional retention buckets**

On the LXC:

```bash
influx bucket create -o combsense -n combsense_1h -r 365d
influx bucket create -o combsense -n combsense_1d -r 0         # infinite
```

Verify: `influx bucket list -o combsense` → three buckets.

- [ ] **Step 3.2: Create the 1-hour downsample task**

Save to `/tmp/downsample-1h.flux` on the LXC:

```flux
option task = {name: "combsense downsample 1h", every: 15m, offset: 1m}

from(bucket: "combsense")
  |> range(start: -task.every * 2, stop: -task.offset)
  |> filter(fn: (r) => r._measurement == "sensor_reading")
  |> aggregateWindow(every: 1h, fn: mean, createEmpty: false)
  |> set(key: "_measurement", value: "sensor_reading_1h")
  |> to(bucket: "combsense_1h", org: "combsense")
```

Register it:

```bash
influx task create -f /tmp/downsample-1h.flux -o combsense
```

Verify: `influx task list -o combsense` → shows the task; after 15 min, `influx query 'from(bucket:"combsense_1h") |> range(start:-2h) |> limit(n:5)'` returns rows.

- [ ] **Step 3.3: Create the 1-day downsample task**

Same pattern, every 6h, source from `combsense_1h`:

```flux
option task = {name: "combsense downsample 1d", every: 6h, offset: 10m}

from(bucket: "combsense_1h")
  |> range(start: -task.every * 2, stop: -task.offset)
  |> filter(fn: (r) => r._measurement == "sensor_reading_1h")
  |> aggregateWindow(every: 1d, fn: mean, createEmpty: false)
  |> set(key: "_measurement", value: "sensor_reading_1d")
  |> to(bucket: "combsense_1d", org: "combsense")
```

- [ ] **Step 3.4: Commit Flux sources to repo for reference**

```bash
mkdir -p deploy/tsdb
# copy both flux files from LXC into deploy/tsdb/
git add deploy/tsdb/downsample-1h.flux deploy/tsdb/downsample-1d.flux
git commit -m "docs(tsdb): snapshot downsample Flux tasks"
```

- [ ] **Step 3.5: Set up LVM snapshot + influx backup**

Two layers: Influx's logical backup (good for restoring data), LVM snapshot (good for restoring the whole LXC).

Create `/usr/local/bin/combsense-backup.sh` on the LXC:

```bash
#!/bin/bash
set -euo pipefail
BACKUP_DIR=/var/backups/combsense-tsdb
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$BACKUP_DIR"
influx backup "$BACKUP_DIR/$STAMP" --token "$INFLUX_TOKEN"
# Keep only the last 14 daily backups
find "$BACKUP_DIR" -maxdepth 1 -type d -mtime +14 -name '20*' -exec rm -rf {} +
```

`chmod 700 /usr/local/bin/combsense-backup.sh`. Put the admin token in `/etc/default/combsense-backup` (mode 600):

```
INFLUX_TOKEN=<admin-token>
```

Systemd unit `/etc/systemd/system/combsense-backup.service`:

```ini
[Unit]
Description=Backup combsense InfluxDB
After=influxdb.service

[Service]
Type=oneshot
EnvironmentFile=/etc/default/combsense-backup
ExecStart=/usr/local/bin/combsense-backup.sh
```

Timer `/etc/systemd/system/combsense-backup.timer`:

```ini
[Unit]
Description=Daily combsense backup

[Timer]
OnCalendar=daily
Persistent=true

[Install]
WantedBy=timers.target
```

Enable: `systemctl enable --now combsense-backup.timer`. Verify: `systemctl list-timers combsense-backup` shows next run; manually run once with `systemctl start combsense-backup.service` and check `/var/backups/combsense-tsdb/` for a dated directory.

---

## Task 4: Firmware emits real epoch timestamps

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/main.cpp`
- Modify: `firmware/sensor-tag-wifi/src/payload.cpp`
- Test: `firmware/sensor-tag-wifi/test/test_payload/test_payload.cpp` (extend)

**Why it matters:** Today firmware sends `t:0`. Telegraf's `timestamp_path = "t"` with `unix` format treats 0 as epoch (1970-01-01) — any real point with `t:0` would land wildly in the past and break graphs. So `optional` on the timestamp parser doesn't help us; we either emit real `t` or Telegraf has to fall back. The cleanest path: when NTP has sync'd, emit real seconds; when it hasn't (first wake, DNS failure, etc.), continue emitting 0 and rely on arrival-time (we'll change Telegraf to treat 0 as "use arrival").

Two changes needed: firmware emits correct `t`, and Telegraf config handles `t:0` fallback.

- [ ] **Step 4.1: Add NTP sync to main.cpp after WiFi connect**

Read current `setup()` → find the block right after `WifiManager::connect()` returns success. Add:

```cpp
// NTP sync — non-blocking, timeout 3s. If this fails we ship t=0 and
// Telegraf stamps with arrival time. Not fatal either way.
configTime(0, 0, "pool.ntp.org", "time.nist.gov");
struct tm tm;
getLocalTime(&tm, 3000);  // waits up to 3s for sync
time_t now = time(nullptr);
Serial.printf("[MAIN] ntp t=%ld\n", (long)now);
```

- [ ] **Step 4.2: Write the failing test**

Extend `test/test_payload/test_payload.cpp` with:

```cpp
void test_payload_includes_epoch_when_set() {
    Reading r;
    r.t1 = 22.5; r.t2 = 18.0; r.battery = 80;
    r.timestamp_epoch = 1713571200;  // 2024-04-19T22:00:00Z
    char buf[PAYLOAD_MAX_LEN];
    int n = Payload::serialize("c5fffe12", r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"t\":1713571200"));
}

void test_payload_t_zero_when_unset() {
    Reading r;
    r.t1 = 22.5;
    // timestamp_epoch left default (0)
    char buf[PAYLOAD_MAX_LEN];
    Payload::serialize("c5fffe12", r, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"t\":0"));
}
```

Register both tests in `RUN_TEST(...)` block at bottom of file.

- [ ] **Step 4.3: Run — expect failure**

```bash
cd firmware/sensor-tag-wifi && pio test -e native
```

Expected: `test_payload_includes_epoch_when_set` fails (Reading struct has no `timestamp_epoch` field yet).

- [ ] **Step 4.4: Implement — add field + serialize it**

In `firmware/sensor-tag-wifi/include/reading.h`, add to `struct Reading`:

```cpp
uint32_t timestamp_epoch = 0;  // Seconds since 1970. 0 = unknown (NTP not sync'd).
```

In `firmware/sensor-tag-wifi/src/payload.cpp`, change the serialization to include `t` from this field instead of hardcoded 0:

```cpp
// Find the existing line that writes "t":0 and replace with:
int n = snprintf(buf, len,
    "{\"id\":\"%s\",\"t\":%u,\"t1\":%.2f,\"t2\":%.2f,\"b\":%d}",
    deviceId, r.timestamp_epoch, r.t1, r.t2, r.battery);
```

(If `t2` is `NaN`, the `%.2f` prints `nan` which is already how we publish; the iOS parser already handles this and so does Telegraf's `optional` field.)

- [ ] **Step 4.5: Wire epoch into main.cpp sampleAndEnqueue**

Where `sampleAndEnqueue()` (or its equivalent — verify current function name) builds a `Reading r`, add right before the enqueue / publish:

```cpp
time_t now = time(nullptr);
// If NTP hasn't sync'd, time(nullptr) returns seconds since boot on ESP-IDF.
// Guard: treat anything before 2024-01-01 (1704067200) as "unset".
r.timestamp_epoch = (now > 1704067200) ? (uint32_t)now : 0;
```

- [ ] **Step 4.6: Rerun tests**

```bash
pio test -e native
```

Expected: all tests pass, including the two new ones.

- [ ] **Step 4.7: Build all firmware envs to catch missed integration**

```bash
pio run -e xiao-c6-ds18b20 && pio run -e xiao-c6-sht31
```

Both expected SUCCESS.

- [ ] **Step 4.8: Flash to bench C6, verify via MQTT sniff**

```bash
bash /tmp/reflash2.sh
mosquitto_sub -h 192.168.1.82 -u hivesense -P hivesense -t 'combsense/hive/+/reading' -v
```

Expected: after the bench sensor's next wake, the published payload's `t` is a plausible current epoch (e.g. `1713571200+`), not 0. If it's 0, NTP didn't sync — check firmware serial logs for the `[MAIN] ntp t=...` line.

- [ ] **Step 4.9: Update Telegraf config to handle t=0 fallback**

On the LXC, edit `/etc/telegraf/telegraf.d/combsense.conf`. Change the JSON config so `t=0` falls back to arrival time. Telegraf's `json_v2` doesn't have a direct "if 0 use current time" option, but you can use a `[[processors.date]]` pattern or — simpler — split into two parsers by prefix OR drop `timestamp_path` entirely and just always use arrival time, then store the firmware's `t` as an additional field `sensor_ts`.

**Recommended change:** remove `timestamp_path`/`timestamp_format` from the parser, add `t` as a regular int field. The point's Influx timestamp becomes arrival (Telegraf stamps). Store the sensor's own timestamp as a queryable field for forensics. This keeps data clean and graph-friendly even during offline-batched drains, at the cost of a slight drift (seconds to minutes) between sense and record time:

```toml
    # Remove these two lines:
    # timestamp_path = "t"
    # timestamp_format = "unix"

    [[inputs.mqtt_consumer.json_v2.field]]
      path = "t"
      rename = "sensor_ts"
      type = "int"
```

**Alternative if you want sense-time accuracy on RTC-buffered drains:** keep `timestamp_path = "t"` and add a Telegraf starlark processor that maps `t==0` to `time.time()`. Pick this only if offline-buffered reading accuracy matters more than operational simplicity.

Reload: `systemctl restart telegraf`, tail the journal to confirm no errors.

- [ ] **Step 4.10: Commit firmware changes**

```bash
git add firmware/sensor-tag-wifi/include/reading.h \
        firmware/sensor-tag-wifi/src/main.cpp \
        firmware/sensor-tag-wifi/src/payload.cpp \
        firmware/sensor-tag-wifi/test/test_payload/test_payload.cpp
git commit -m "feat(sensor-tag-wifi): NTP sync and epoch timestamp in payload

Firmware now syncs NTP after WiFi connect (3s timeout) and emits the
current epoch as \`t\` in the reading payload. If NTP hasn't sync'd,
emits t=0 so downstream can fall back to arrival time. Unlocks
accurate historical graphing from InfluxDB — without this, offline-
batched readings from the RTC ring buffer all stamp at drain time."
```

---

## Task 5: iOS HistoryService + chart view

**Files:**
- Create: `CombSense/Models/HistoryReading.swift`
- Create: `CombSense/Services/HistoryService.swift`
- Create: `CombSense/Features/Hive/HiveHistoryView.swift`
- Modify: `CombSense/Features/Settings/CloudMonitoringView.swift` (add Influx fields)
- Modify: `CombSense/Features/Hive/HiveDetailView.swift` (link to history view; adjust path if detail view is differently named)
- Create: `CombSenseTests/Services/HistoryServiceTests.swift`

**Note:** These steps are to be dispatched to the Claude session inside the iOS app repo. The controller here provides the full step text each time.

- [ ] **Step 5.1: Add Influx settings to CloudMonitoringView**

Extend the existing Cloud Monitoring settings pane to include Influx fields. Use @AppStorage for host and org, @AppStorage + Keychain for the read token (never AppStorage for secrets). Model after the existing MQTT fields.

Fields to add:
- `Influx URL` — e.g. `http://192.168.1.100:8086`
- `Organization` — `combsense`
- `Read Token` — stored in Keychain under key `combsense.influx.readToken`

No test yet; tested through the service layer (Step 5.4).

- [ ] **Step 5.2: Create HistoryReading model**

```swift
// CombSense/Models/HistoryReading.swift
import Foundation

/// Read-only projection of an Influx row. Not SwiftData — this is transient
/// chart data, not something we persist locally. One `HistoryReading` per
/// time bucket; fields are optional because Influx aggregation may omit nil
/// fields when no samples existed in a window.
struct HistoryReading: Identifiable, Hashable {
    let id: Date            // timestamp doubles as identity for charts
    let timestamp: Date
    let tempInternal: Double?
    let tempExternal: Double?
    let batteryPercent: Int?
}
```

- [ ] **Step 5.3: Write the failing tests for HistoryService**

Create `CombSenseTests/Services/HistoryServiceTests.swift`:

```swift
import XCTest
@testable import CombSense

final class HistoryServiceTests: XCTestCase {
    var session: URLSessionMock!
    var service: HistoryService!

    override func setUp() {
        session = URLSessionMock()
        service = HistoryService(
            baseURL: URL(string: "http://tsdb.local:8086")!,
            org: "combsense",
            token: "read-token-abc",
            session: session
        )
    }

    func testFluxQueryForLast24h() async throws {
        session.nextResponse = .success(mockCSV(rows: [
            ("2026-04-19T10:00:00Z", "t1", 22.1),
            ("2026-04-19T11:00:00Z", "t1", 22.4)
        ]))

        _ = try await service.fetchReadings(
            sensorId: "c5fffe12",
            range: .last24h,
            resolution: .auto
        )

        let body = String(data: session.lastRequest!.httpBody!, encoding: .utf8)!
        XCTAssertTrue(body.contains(#"r.sensor_id == "c5fffe12""#))
        XCTAssertTrue(body.contains("range(start: -24h)"))
        XCTAssertEqual(
            session.lastRequest!.value(forHTTPHeaderField: "Authorization"),
            "Token read-token-abc"
        )
    }

    func testDecodesMultipleFields() async throws {
        session.nextResponse = .success(mockCSV(rows: [
            ("2026-04-19T10:00:00Z", "t1", 22.1),
            ("2026-04-19T10:00:00Z", "t2", 18.3),
            ("2026-04-19T10:00:00Z", "b", 85)
        ]))

        let readings = try await service.fetchReadings(
            sensorId: "c5fffe12", range: .last24h, resolution: .auto
        )

        XCTAssertEqual(readings.count, 1)
        XCTAssertEqual(readings[0].tempInternal, 22.1)
        XCTAssertEqual(readings[0].tempExternal, 18.3)
        XCTAssertEqual(readings[0].batteryPercent, 85)
    }

    func testHTTPErrorSurfacesAsThrown() async {
        session.nextResponse = .failure(HTTPError(status: 401))
        do {
            _ = try await service.fetchReadings(
                sensorId: "c5fffe12", range: .last24h, resolution: .auto
            )
            XCTFail("Expected throw")
        } catch let HTTPError.badStatus(code) {
            XCTAssertEqual(code, 401)
        } catch { XCTFail("Wrong error: \(error)") }
    }
}
```

Include a tiny `URLSessionMock` and `mockCSV(rows:)` helper at the bottom of the test file. Full mock and helper code belong in the file — do not placeholder them.

Run: `xcodebuild test -scheme CombSense -destination 'platform=iOS Simulator,name=iPhone 15'` → expect compile failure because `HistoryService` doesn't exist yet.

- [ ] **Step 5.4: Implement HistoryService**

```swift
// CombSense/Services/HistoryService.swift
import Foundation

enum TimeRange {
    case last24h, last7d, last30d, last1y
    var fluxRange: String {
        switch self {
        case .last24h: return "-24h"
        case .last7d:  return "-7d"
        case .last30d: return "-30d"
        case .last1y:  return "-365d"
        }
    }
}

enum Resolution {
    case auto  // Influx downsampled bucket picks itself based on range
    case raw
    case hourly
    case daily
}

struct HTTPError: Error, Equatable {
    let status: Int
    static func badStatus(_ code: Int) -> HTTPError { HTTPError(status: code) }
}

protocol URLSessionProtocol {
    func data(for request: URLRequest) async throws -> (Data, URLResponse)
}
extension URLSession: URLSessionProtocol {}

final class HistoryService {
    private let baseURL: URL
    private let org: String
    private let token: String
    private let session: URLSessionProtocol

    init(baseURL: URL, org: String, token: String, session: URLSessionProtocol = URLSession.shared) {
        self.baseURL = baseURL
        self.org = org
        self.token = token
        self.session = session
    }

    func fetchReadings(
        sensorId: String,
        range: TimeRange,
        resolution: Resolution
    ) async throws -> [HistoryReading] {
        let bucket = bucketFor(range: range, resolution: resolution)
        let flux = """
        from(bucket: "\(bucket)")
          |> range(start: \(range.fluxRange))
          |> filter(fn: (r) => r.sensor_id == "\(sensorId)")
          |> filter(fn: (r) => r._field == "t1" or r._field == "t2" or r._field == "b")
          |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
        """

        var url = URLComponents(url: baseURL.appendingPathComponent("/api/v2/query"), resolvingAgainstBaseURL: false)!
        url.queryItems = [URLQueryItem(name: "org", value: org)]

        var req = URLRequest(url: url.url!)
        req.httpMethod = "POST"
        req.setValue("Token \(token)", forHTTPHeaderField: "Authorization")
        req.setValue("application/vnd.flux", forHTTPHeaderField: "Content-Type")
        req.setValue("application/csv", forHTTPHeaderField: "Accept")
        req.httpBody = flux.data(using: .utf8)

        let (data, resp) = try await session.data(for: req)
        guard let http = resp as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
            throw HTTPError(status: (resp as? HTTPURLResponse)?.statusCode ?? -1)
        }

        return try parseCSV(data)
    }

    private func bucketFor(range: TimeRange, resolution: Resolution) -> String {
        switch (range, resolution) {
        case (_, .raw):    return "combsense"
        case (_, .hourly): return "combsense_1h"
        case (_, .daily):  return "combsense_1d"
        case (.last24h, .auto), (.last7d, .auto): return "combsense"
        case (.last30d, .auto): return "combsense_1h"
        case (.last1y, .auto):  return "combsense_1d"
        }
    }

    private func parseCSV(_ data: Data) throws -> [HistoryReading] {
        // Influx CSV: annotated header rows start with #. Real rows have fixed
        // columns. We pivoted in Flux, so each row has: _time, t1, t2, b, plus
        // metadata columns. Rather than write a full RFC4180 parser, walk the
        // bytes — Influx escapes commas inside fields per CSV rules, which
        // we don't expect for numeric data.
        let text = String(decoding: data, as: UTF8.self)
        var readings: [HistoryReading] = []
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]

        var header: [String] = []
        for raw in text.split(separator: "\n", omittingEmptySubsequences: true) {
            let line = String(raw)
            if line.hasPrefix("#") { continue }
            let cols = line.components(separatedBy: ",")
            if header.isEmpty { header = cols; continue }
            guard cols.count == header.count else { continue }

            func col(_ name: String) -> String? {
                guard let idx = header.firstIndex(of: name) else { return nil }
                return cols[idx].isEmpty ? nil : cols[idx]
            }

            guard let tsRaw = col("_time"), let ts = formatter.date(from: tsRaw) else { continue }
            let t1 = col("t1").flatMap(Double.init)
            let t2 = col("t2").flatMap(Double.init)
            let b  = col("b").flatMap(Int.init)
            readings.append(HistoryReading(
                id: ts, timestamp: ts,
                tempInternal: t1, tempExternal: t2, batteryPercent: b
            ))
        }
        return readings
    }
}
```

- [ ] **Step 5.5: Run tests — expect pass**

```bash
xcodebuild test -scheme CombSense -destination 'platform=iOS Simulator,name=iPhone 15'
```

All HistoryServiceTests pass.

- [ ] **Step 5.6: Build HiveHistoryView with Swift Charts**

```swift
// CombSense/Features/Hive/HiveHistoryView.swift
import SwiftUI
import Charts

struct HiveHistoryView: View {
    let hive: Hive
    @State private var range: TimeRange = .last24h
    @State private var readings: [HistoryReading] = []
    @State private var loading = false
    @State private var error: String?

    var body: some View {
        VStack {
            Picker("Range", selection: $range) {
                Text("24h").tag(TimeRange.last24h)
                Text("7d").tag(TimeRange.last7d)
                Text("30d").tag(TimeRange.last30d)
                Text("1y").tag(TimeRange.last1y)
            }
            .pickerStyle(.segmented)
            .padding(.horizontal)

            if loading {
                ProgressView().frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if let error {
                Text(error).foregroundStyle(.red).padding()
            } else {
                Chart(readings) { r in
                    if let t1 = r.tempInternal {
                        LineMark(x: .value("Time", r.timestamp),
                                 y: .value("Internal °C", t1))
                            .foregroundStyle(by: .value("Field", "Internal"))
                    }
                    if let t2 = r.tempExternal {
                        LineMark(x: .value("Time", r.timestamp),
                                 y: .value("External °C", t2))
                            .foregroundStyle(by: .value("Field", "External"))
                    }
                }
                .padding()
            }
        }
        .navigationTitle("History")
        .task(id: range) { await reload() }
        .refreshable { await reload() }
    }

    private func reload() async {
        guard let sensorId = hive.sensorMacAddress else {
            error = "No sensor ID on this hive"
            return
        }
        guard let svc = HistoryService.fromSettings() else {
            error = "Configure Influx in Settings"
            return
        }
        loading = true; error = nil
        defer { loading = false }
        do {
            readings = try await svc.fetchReadings(
                sensorId: sensorId, range: range, resolution: .auto
            )
        } catch {
            self.error = "\(error)"
        }
    }
}
```

Add a `HistoryService.fromSettings()` convenience that pulls the URL, org, and Keychain token.

- [ ] **Step 5.7: Wire a navigation entry from HiveDetailView**

Add a `NavigationLink` button/row labeled "History" on `HiveDetailView` that pushes `HiveHistoryView(hive: hive)`. Disable (or show a hint) if `sensorMacAddress` is nil.

- [ ] **Step 5.8: End-to-end test on device**

Run the app on the iPhone. Select a hive with `sensorMacAddress = c5fffe12`, tap History, verify:
- 24h view renders with t1 line
- 7d view downsamples to hourly (still looks smooth — Flux returned pre-aggregated data)
- Pull-to-refresh reloads
- Removing the Influx token in Settings surfaces an error, not a crash

- [ ] **Step 5.9: Commit iOS changes**

On the iOS repo's `dev` branch:

```
feat(history): InfluxDB-backed history graph

Adds HistoryService that queries InfluxDB's /api/v2/query HTTP API
with Flux, pivoted by _field so t1/t2/b land as columns. Swift Charts
view in HiveHistoryView with 24h/7d/30d/1y range selector and auto-
resolution (raw → 1h → 1d based on range). Token stored in Keychain,
Influx URL + org in Settings alongside MQTT.
```

---

## Ordering

- **Task 1** first (must exist before Telegraf or Grafana can be installed).
- **Task 2** after Task 1 (needs token + bucket).
- **Task 4** can run in parallel with Task 2 — firmware change doesn't depend on the LXC being up. But **Step 4.9** (Telegraf t=0 handling) requires Task 2 complete.
- **Task 3** after Task 2 (need data flowing to configure retention/downsampling on).
- **Task 5** after Tasks 2 + 3 (needs Influx reachable + downsampled buckets named).

## Self-review

- Spec coverage: all four threads from the conversation (LXC + stack, Telegraf, firmware `t`, iOS) mapped to tasks. Backups and downsampling added per 50-hive scale ask.
- Placeholder scan: zero TBD/TODO/fill-in. Every code block compiles (Swift + C++) or runs (bash + TOML + Flux) as-is, modulo the tokens the reader fills in.
- Type consistency: `HistoryReading` fields (`tempInternal`, `tempExternal`, `batteryPercent`) mirror SensorReading — app-side naming stays consistent across persisted + transient data.
- Cross-system paths: where a step crosses hosts (Proxmox shell, LXC, dev machine, iOS repo) the boundary is stated.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-19-sensor-history.md`. Two execution options:

**1. Subagent-Driven (recommended for Tasks 4-5)** — dispatch fresh subagent per task, review between tasks.

**2. Inline Execution** — walk through tasks interactively. Best for Tasks 1-3 since they're host-ops work where you'll be watching output.

Suggested mix: **you drive Tasks 1-3 interactively** (hands on Proxmox), then **switch to subagent-driven for Task 4** (firmware in this repo) and relay Task 5 steps to the iOS Claude session. Which approach?
