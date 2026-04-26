---
name: conventions
description: Coding conventions and project patterns
triggers:
  - "convention"
  - "code style"
  - "naming"
  - "best practice"
  - "verify"
edges:
  - target: context/architecture.md
    condition: when conventions apply to a specific component or layer
  - target: context/stack.md
    condition: when a convention is tied to a specific library or framework
  - target: patterns/INDEX.md
    condition: when looking for a task-specific verify checklist
last_updated: 2026-04-26
---

# Conventions

## Project Structure

```
combsense-monitor/
  firmware/
    hive-node/       — Freenove ESP32-S3 Lite hive node firmware
    collector/       — LilyGO T-SIM7080G yard collector firmware
    shared/          — Headers shared between both firmwares (HivePayload, OTA protocol)
  hardware/          — Schematics, PCB layouts, 3D print files
  docs/              — Design specs, implementation plans
  README.md          — Hardware datasheet (primary design doc)
```

## Engineering Principles

- **SOLID, DRY, KISS** — prioritize long-term maintainability over clever one-liners
- **Descriptive semantic naming** — `calculateTotalBalance` not `calc`, `readInternalTemperature` not `readTemp1`
- **Small focused functions** — single responsibility per function
- **Guard clauses over nesting** — avoid deeply nested loops or complex ternaries
- **Const-correctness** — mark parameters `const&` when not modified
- **RAII** — tie resource lifetime to scope (file handles, peripheral power, buffers)
- **Modern C++ idioms** — prefer STL algorithms over manual loops where intention is clearer
- **Meaningful inline comments** — explain "why" only where not obvious from the code
- **Docstrings for public methods** — document interface, parameters, and return values
- **Explicit ownership** — clear who creates, owns, and destroys resources

## Firmware Conventions (ESP32/Arduino)

- Use PlatformIO for build management
- C/C++ with Arduino framework
- Modular files: one file per sensor/subsystem
- Each module exposes `initialize()`, `readMeasurements()`, `enterSleep()` interface
- Central state machine dispatcher calls into modules
- Use deep sleep and power gating aggressively
- MOSFET power control lives in each module's `enterSleep()`/`initialize()`
- Store config in NVS (non-volatile storage)
- ESP-NOW payload struct defined in shared header
- Prefer `std::accumulate`, `std::transform` etc. over raw loops when clearer
- Use `explicit` on single-argument constructors
- Use `const std::string&` over `String` where possible (avoid Arduino String fragmentation)

## Branching Strategy

- `main` — stable baseline
- `dev` — integration branch
- Feature branches off `dev` with descriptive names
- PRs to `dev`, `dev → main` at milestones

## Verify Checklist

Run after every firmware change:
1. PlatformIO build succeeds
2. Upload to test ESP32 and verify serial output
3. Sensor readings are reasonable
4. Deep sleep current measured (target: <15 µA bare chip)
5. ESP-NOW transmission confirmed by collector
