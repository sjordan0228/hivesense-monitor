---
name: agents
description: Always-loaded project anchor. Read this first. Contains project identity, non-negotiables, commands, and pointer to ROUTER.md for full context.
last_updated: 2026-04-26
---

# CombSense Monitor

## What This Is
IoT beehive telemetry system: ESP32 sensor nodes (weight, temp/humidity, planned bee-traffic counter) communicate via ESP-NOW to a yard collector, then via cellular MQTT to HiveMQ Cloud — and via direct WiFi/MQTT to a local Mosquitto/Influx/Grafana stack — for iOS and Django web consumption.

## Non-Negotiables
- Never commit directly to `main` — work on `dev`, PR to `main` ("prod" = `main`)
- Never run `pio run` or `pio test` raw — always silence/grep output (compile spam is the biggest avoidable context cost)
- Never use Bluedroid for BLE — NimBLE only (528 KB flash savings is required for dual OTA partitions)
- Always run the Ollama qwen3-coder code review before opening a PR — mandatory, not optional
- Never bypass three-token Influx auth (admin / telegraf-write / ios-read) — the iOS token must remain read-only

## Commands
- Build firmware (silent): `pio run -s 2>&1 | tail -5`
- Native unit tests (silent): `pio test -e native 2>&1 | grep -E "(PASS|FAIL|Tests|Ignored)"`
- Upload + serial: `pio run -t upload && pio device monitor`
- Web app dev: `cd web && python manage.py runserver`
- Web app tests: `cd web && python manage.py test`
- Drift check: `mex check`

## Scaffold Growth
After every task: if no pattern exists for the task type you just completed, create one. If a pattern or context file is now out of date, update it. The scaffold grows from real work, not just setup. See the GROW step in `ROUTER.md` for details.

## Navigation
At the start of every session, read `ROUTER.md` before doing anything else.
For full project context, patterns, and task guidance — everything is there.
