---
name: setup
description: Dev environment setup and commands. Load when setting up the project for the first time or when environment issues arise.
triggers:
  - "setup"
  - "install"
  - "environment"
  - "getting started"
  - "how do I run"
  - "local development"
edges:
  - target: context/stack.md
    condition: when specific technology versions or library details are needed
  - target: context/architecture.md
    condition: when understanding how components connect during setup
  - target: patterns/INDEX.md
    condition: when looking for task-specific build/upload/provision patterns
last_updated: 2026-04-26
---

# Setup

## Prerequisites

- **PlatformIO Core** (firmware builds): `pip install platformio` or via the VSCode PlatformIO extension
- **Python 3.11+** (web app + provisioning + tests). 3.14 is fine locally; LXC runs 3.11
- **Node 20+** (only for the `mex` / `promexeus` CLI)
- **macOS / Linux dev host** — sensor-tag-wifi USB-CDC requires `/dev/cu.usbmodem*` style ports
- **SSH access**: `root@192.168.1.19` (combsense-tsdb LXC, key-based) and `natas@192.168.1.61` (combsense-web LXC, key + sudo)

## First-time Setup

**Firmware (sensor-tag-wifi example, mirror for hive-node / collector / sensor-tag):**

1. `git clone git@github.com:sjordan0228/combsense-monitor.git && cd combsense-monitor`
2. `cd firmware/sensor-tag-wifi`
3. Verify build: `pio run -s 2>&1 | tail -5`
4. Verify native tests: `pio test -e native 2>&1 | grep -E "(PASS|FAIL|Tests|Ignored)"`
5. Provision a connected device: `python ../../tools/provision_tag.py --port /dev/cu.usbmodem* --ssid <WIFI> --psk <PSK> --mqtt-host 192.168.1.82 --mqtt-user hivesense --mqtt-pass <PW> --ota-host http://192.168.1.61`
6. Upload + monitor: `pio run -t upload --upload-port /dev/cu.usbmodem* && pio device monitor`

**Web app (combsense-web):**

1. `cd web`
2. `python -m venv .venv && source .venv/bin/activate`
3. `pip install -r requirements.txt`
4. Copy `deploy/web/env.template` → `web/.env`, fill in values
5. `python manage.py migrate`
6. `python manage.py runserver`

## Environment Variables

Web app (`web/.env`):

- `DJANGO_SECRET_KEY` (required) — Django session/CSRF signing key
- `DATABASE_URL` (required) — Postgres URL (`postgres://user:pass@host:5432/combsense`)
- `DJANGO_ALLOWED_HOSTS` (required for prod)
- `DJANGO_DEBUG` (default `0` in prod)
- `DJANGO_SECURE_COOKIES` (`1` in prod)
- `DJANGO_CSRF_TRUSTED_ORIGINS` (required if behind nginx proxy)
- `DJANGO_EMAIL_BACKEND` (`console.EmailBackend` for dev; SMTP for prod)

Influx tokens (combsense-tsdb LXC, `/root/.combsense-tsdb-creds`, mode 600):

- `admin_token` — setup + backup only, never leaves the LXC
- `telegraf_write_token` — write-only, scoped to `combsense` bucket
- `ios_read_token` — read-only, whole org

Web creds (combsense-web LXC, `/root/.combsense-web-creds`, mode 600).

## Common Commands

- `pio run -s 2>&1 | tail -5` — silent firmware build (3 size lines + status)
- `pio test -e native 2>&1 | grep -E "(PASS|FAIL|Tests|Ignored)"` — silent native unit tests
- `pio run -t upload --upload-port /dev/cu.usbmodem*` — flash a connected device
- `pio device monitor --port /dev/cu.usbmodem*` — serial monitor (USB-CDC, 115200 baud)
- `cd web && python manage.py test` — Django tests (19 currently passing)
- `mex check` — drift score for `.mex/` scaffold
- `ssh natas@192.168.1.16 "cd ~/repos/hivesense-monitor && git fetch origin && git checkout <branch> && ~/code-review/review.sh ~/repos/hivesense-monitor <base-branch> qwen3-coder:30b"` — pre-PR Ollama review (mandatory)
- `curl -s http://192.168.1.16:11434/api/generate -d '{"model":"qwen3-coder:30b","prompt":"<prompt>","stream":false}'` — Ollama for boilerplate generation

## Common Issues

- **C6 OTA fails with `EAI_FAIL` / error 202**: don't use `esp_http_client`; use raw `WiFiClient` + `IPAddress::fromString`. The C6 routes hostname lookups through OpenThread DNS64.
- **MQTT publish silently truncates after WiFi teardown**: call `client.flush()` (or wait for `loop()`) before `WiFi.disconnect()`. PubSubClient buffers TX.
- **Telegraf rejects payloads with "missing field"**: any new payload field must be marked `optional = true` in `deploy/tsdb/telegraf-combsense.conf` during a rolling firmware update.
- **Pre-NTP timestamps land as `t=0`**: this is correct. Telegraf overwrites with arrival time; firmware `t` is preserved as `sensor_ts` for forensics.
- **Unprivileged LXC service fails with `226/NAMESPACE`**: stock Debian 12 systemd units use `PrivateMounts=true` etc. that the container can't satisfy. Add a drop-in at `/etc/systemd/system/<svc>.service.d/override.conf` setting each directive permissively.
- **MH-CD42 boost shuts off during deep sleep**: load <45 mA for >32 s trips auto-shutoff. Don't use this module for sleep-based loads — use TP4056 + low-dropout LDO instead.
