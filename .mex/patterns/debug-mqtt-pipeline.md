---
name: debug-mqtt-pipeline
description: Debug pattern for when sensor readings don't reach Grafana/iOS. Walks the firmware → Mosquitto → Telegraf → Influx → Grafana flow boundary by boundary.
triggers:
  - "no data in grafana"
  - "missing readings"
  - "mqtt not working"
  - "telegraf"
  - "influx empty"
edges:
  - target: context/architecture.md
    condition: when needing the full data-flow diagram
  - target: context/setup.md
    condition: when checking systemd service status or log locations
last_updated: 2026-04-26
---

# Debug the MQTT → Influx → Grafana Pipeline

## Context

Five components, four boundaries. When sensor readings stop appearing in Grafana or iOS, walk each boundary in order. Don't guess — confirm at each hop.

Data flow:

```
sensor-tag-wifi  →  Mosquitto  →  Telegraf  →  InfluxDB  →  Grafana / iOS
   (firmware)    (192.168.1.82)  (combsense-tsdb LXC, 192.168.1.19)
```

## Steps

**Boundary 1 — Is the firmware actually publishing?**

```bash
pio device monitor --port /dev/cu.usbmodem*
```

Look for: WiFi associate, MQTT connect, "publish ok", deep sleep entry. If WiFi but no MQTT, check broker creds. If no WiFi, check RSSI / SSID.

**Boundary 2 — Is Mosquitto receiving?**

```bash
mosquitto_sub -h 192.168.1.82 -u hivesense -P "<pw>" -t "combsense/hive/+/reading" -v
```

You should see live publishes from each active sensor_id. If silent, the broker isn't getting the publish. Check firmware MQTT credentials and network reachability.

**Boundary 3 — Is Telegraf consuming?**

On combsense-tsdb LXC (`ssh root@192.168.1.19`):

```bash
journalctl -u telegraf -n 50 --no-pager
systemctl status telegraf
```

Look for "subscribed to combsense/hive/+/reading" at startup, and JSON parse messages on each publish. Errors like "missing field" mean a payload field changed without updating `optional = true` in `/etc/telegraf/telegraf.d/combsense.conf`.

**Boundary 4 — Is Influx receiving writes?**

```bash
ssh root@192.168.1.19 \
  "source /root/.combsense-tsdb-creds && \
   influx query 'from(bucket:\"combsense\") |> range(start:-15m) \
                  |> filter(fn:(r) => r._measurement == \"sensor_reading\") \
                  |> count()' --token \$admin_token --org combsense"
```

Should return non-zero counts for each active `sensor_id`. If empty: Telegraf wrote to a different bucket / retention policy rejected the timestamp / token expired.

**Boundary 5 — Is Grafana querying correctly?**

In the dashboard, edit the affected panel and run the Flux query inline. Check the `sensor_id` template variable is set to `All` (or the right specific tag). Confirm the time range is correct — default `now-24h to now`.

## Gotchas

- **`t=0` from pre-NTP firmware:** Telegraf overwrites with arrival time. Firmware `t=0` would otherwise map to epoch 1970 and Influx would reject. The original `t` is preserved as field `sensor_ts` for forensics.
- **Adding new payload fields requires Telegraf config update** with `optional = true` BEFORE rolling out new firmware. Otherwise Telegraf drops the entire record (not just the new field).
- **Three-bucket setup:** `combsense` (raw, 30d), `combsense_1h` (365d), `combsense_1d` (∞). Recent data lives in raw. Old data is downsampled. iOS auto-picks bucket based on range, so a Grafana-vs-iOS discrepancy may be a bucket selection issue.
- **Downsample tasks keep `_measurement = "sensor_reading"`** in all three buckets — don't rename. iOS Flux query depends on it.
- **Unprivileged LXC + systemd sandboxing:** if telegraf or grafana fail with `226/NAMESPACE`, the drop-in at `/etc/systemd/system/<svc>.service.d/override.conf` is the fix. Don't switch to a privileged container.

## Verify

- [ ] After fix: `mosquitto_sub` sees the publish
- [ ] `journalctl -u telegraf -f` shows the matching ingest line
- [ ] Influx count query returns non-zero for the affected `sensor_id` in the last 15 min
- [ ] Grafana panel renders without "No data"
- [ ] No `t=0` rejections in Telegraf logs

## Debug

- **All boundaries pass but Grafana is still empty:** clear browser cache; check the Grafana datasource UID matches the dashboard's `${DS_INFLUXDB}` resolution (`cfjpfye54jg1sd` on this stack).
- **Sporadic gaps in data:** check RSSI history for that sensor_id — gaps usually correlate with weak signal. Also check if the tag is solar-charging (battery voltage trend should not be monotonic decreasing during the day).
- **One sensor missing while others work:** firmware-side issue (provisioning, hardware) on that specific tag. Reproduce with `pio device monitor` on that tag's USB.

## Update Scaffold

- [ ] Update `context/architecture.md` "External Dependencies" if a new component is added to the pipeline
- [ ] Update `context/setup.md` "Common Issues" if a new failure mode emerges that other devs would hit
- [ ] If a new boundary is added (e.g. a Celery worker between Telegraf and Influx), update the flow diagram and add a Boundary N step
