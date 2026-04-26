---
name: provision-sensor-tag-wifi
description: Provision a sensor-tag-wifi device with WiFi/MQTT/OTA credentials over the USB-CDC serial console. Use when bringing up a new tag for deployment.
triggers:
  - "provision"
  - "new sensor tag"
  - "deploy tag"
  - "first flash"
edges:
  - target: context/setup.md
    condition: when finding the right port name or env var values
  - target: patterns/pio-build-test.md
    condition: when building the firmware before flashing
last_updated: 2026-04-26
---

# Provision a Sensor-Tag-WiFi Device

## Context

A fresh sensor-tag-wifi (XIAO ESP32-C6 or Waveshare S3-Zero) needs WiFi credentials, MQTT broker info, OTA host, and a `sensor_id` written to NVS before it can publish. Provisioning happens over the USB-CDC serial console using `tools/provision_tag.py`. Once provisioned, the tag persists everything in NVS and OTA updates do not touch this configuration.

The right firmware variant must be flashed first — there are three envs:
- `xiao-c6-sht31` — C6 + SHT31 dual temp/humidity (default)
- `xiao-c6-ds18b20` — C6 + DS18B20 dual probes
- `waveshare-s3zero-ds18b20` — S3-Zero + DS18B20 (NOT recommended for solar/sleep; see ROUTER.md S3-Zero warning)

## Steps

1. **Choose the variant** based on hardware. For yard deployment with 18650 + solar, use a C6 env.
2. **Build firmware silently:** `cd firmware/sensor-tag-wifi && pio run -e xiao-c6-sht31 -s 2>&1 | tail -5` (use `pio-build-test` pattern)
3. **Connect the device** via USB-C. Find the port: `ls /dev/cu.usbmodem*` (macOS) or `ls /dev/ttyACM*` (Linux). On macOS the port may take a beat to enumerate due to a known CDC race.
4. **Flash:** `pio run -e xiao-c6-sht31 -t upload --upload-port /dev/cu.usbmodem*`
5. **Provision over the console:**

   ```bash
   python tools/provision_tag.py \
     --port /dev/cu.usbmodem* \
     --ssid "<your-wifi-ssid>" \
     --psk "<your-wifi-psk>" \
     --mqtt-host 192.168.1.82 \
     --mqtt-user hivesense \
     --mqtt-pass "<password>" \
     --ota-host http://192.168.1.61
   ```

   The script writes WiFi/MQTT/OTA settings to NVS via the firmware's serial command interface. `sensor_id` is auto-derived from the chip MAC unless overridden with `--sensor-id`.

6. **Verify** by watching the serial output: `pio device monitor --port /dev/cu.usbmodem*`. Look for: WiFi associate, MQTT connect, first publish, deep sleep entry.
7. **Confirm in Influx** by querying for the new `sensor_id` in the `combsense` bucket — should appear within one wake cycle (5 min).

## Gotchas

- **Port enumeration race on macOS:** if `provision_tag.py` says "device not found," wait 2-3 seconds after plugging in. The C6's USB-CDC enumerates slower than UART chips.
- **Local Mosquitto only on home network:** `192.168.1.82` is unreachable from cellular yards. For remote deployments, the MQTT host must be the HiveMQ Cloud endpoint (planned, not yet provisioned).
- **OTA host must be reachable from the tag's network:** `http://192.168.1.61` is LAN-only by nginx allowlist. Don't try to OTA from outside the home network without bridging.
- **Don't use the S3-Zero variant for solar/sleep deployments:** AMS1117 LDO needs >4.3V VIN; raw 18650 will brown out on every wake. Use C6 or do the LDO-swap rework first.

## Verify

- [ ] `pio device monitor` shows successful WiFi associate
- [ ] First MQTT publish appears in Mosquitto: `mosquitto_sub -h 192.168.1.82 -u hivesense -P <pw> -t "combsense/hive/+/reading"`
- [ ] Telegraf consumes the publish — check Influx via `curl --header "Authorization: Token $TOKEN" --data-urlencode 'q=...' http://192.168.1.19:8086/api/v2/query`
- [ ] Tag's `sensor_id` appears as a series in Grafana `combsense-home-yard` dashboard within 10 minutes
- [ ] RSSI is in the green band (> -75 dBm); if -75 to -85 marginal, if < -85 reposition antenna

## Debug

- **No WiFi associate:** check SSID/PSK in `provision_tag.py` output; verify with `mosquitto_sub` that the broker is reachable from the tag's network.
- **WiFi up but no MQTT publish:** check `mqtt-user` / `mqtt-pass`; look for "MQTT connect failed" in serial output.
- **Publishes appear in Mosquitto but not Influx:** Telegraf is the suspect — check `journalctl -u telegraf -f` on combsense-tsdb LXC.
- **Influx has data but Grafana shows nothing:** check the `sensor_id` template variable in the dashboard; default is `All`.

## Update Scaffold

- [ ] If a new env is added (e.g. a new board variant), update `context/architecture.md` "Sensor-tag-wifi firmware" section and this pattern's variant list
- [ ] If `provision_tag.py` gains new flags, update the command in step 5
- [ ] If MQTT broker changes (cellular yards land), add a new "Steps for cellular yard" section
