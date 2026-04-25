# Sensor-Tag-Wifi OTA — Design Spec

**Date:** 2026-04-24
**Status:** Approved, ready for plan
**Repo touch points:** `firmware/sensor-tag-wifi/`, `deploy/web/`, `tools/provision_tag.py`

## Goal

Give the home-yard sensor-tag-wifi (XIAO ESP32-C6) **HTTP-pull OTA** so a device deployed at a beehive can be updated without physical access.

The device wakes every ~5 min, reads sensors, publishes MQTT, and deep-sleeps. After the publish, it fetches a small JSON manifest from a local nginx host; if the manifest's version differs from the compiled-in version, it streams a new firmware binary into the inactive OTA partition, verifies SHA-256, and reboots into it. The new firmware proves itself by completing one full publish cycle; otherwise the bootloader rolls back automatically.

## Non-Goals

- OTA for the cellular collector or hive-node (already implemented separately).
- Public/internet-exposed firmware host. This is LAN-only.
- Cryptographic signing. SHA-256 in the manifest covers integrity; signing is deferred until/if firmware ever leaves the LAN.
- Per-device targeting (rolling subsets, canary deploys). Manifest is per-variant; all devices of that variant track it.

## Architecture

**Per-wake flow:**

```
deep sleep
  └─ wake
      ├─ Ota::validateOnBoot()      ← detect rollback, update NVS state
      ├─ sample sensor
      ├─ wifi connect
      ├─ mqtt connect + publish reading
      │     └─ if publish ok: Ota::onPublishSuccess()
      │           └─ if PENDING_VERIFY: mark valid, clear NVS state
      ├─ Ota::checkAndApply(batteryPct)
      │     ├─ GET manifest.json
      │     ├─ skip if manifest.version == FIRMWARE_VERSION
      │     ├─ skip if manifest.version == NVS.failed
      │     ├─ skip if batteryPct < OTA_BATTERY_FLOOR_PCT (20)
      │     ├─ stream-download bin → inactive partition; running SHA-256
      │     ├─ verify SHA-256 == manifest.sha256 (else abort)
      │     ├─ esp_ota_set_boot_partition(inactive)
      │     ├─ NVS.attempted = manifest.version
      │     └─ esp_restart()
      └─ deep sleep
```

**Invariants:**

1. Reading is published *before* the OTA check — never lose a sample to an OTA download.
2. New firmware is only marked valid after a successful publish — bootloader rolls back if `mark_valid` never fires.
3. After a rollback, the old firmware refuses to re-attempt the same version until the manifest moves.
4. Manifest is the single source of truth: change version → all devices in that variant move; revert → they revert.

## Components

### New files

`firmware/sensor-tag-wifi/src/ota.{h,cpp}`
Thin wrapper around `esp_http_client_*` + `esp_ota_*`. Public API:

```cpp
namespace Ota {
    void validateOnBoot();
    void onPublishSuccess();
    void checkAndApply(uint8_t batteryPct);
}
```

All decision logic delegated to pure functions in `ota_decision.cpp` for native testability. `ota.cpp` is the side-effecting shell.

`firmware/sensor-tag-wifi/src/ota_manifest.{h,cpp}`
Pure JSON parser. Fixed-size, no allocations beyond the `Manifest` struct.

```cpp
struct Manifest {
    char version[32];
    char url[256];
    char sha256[65];   // 64 hex + null
    size_t size;
};
bool parseManifest(const char* json, size_t len, Manifest& out);
```

`firmware/sensor-tag-wifi/src/ota_decision.{h,cpp}`
Pure decision functions:

```cpp
bool shouldApply(const char* current, const char* manifest, const char* failed, uint8_t batteryPct);

enum class ValidateAction { NoOp, ClearAttempted, RecordFailed };
ValidateAction validateOnBootAction(const char* firmwareVersion, const char* attempted, bool isPendingVerify);
```

`firmware/sensor-tag-wifi/src/ota_state.{h,cpp}`
Thin `Preferences` adapter. Get/set `attempted`, `failed` strings under namespace `ota`. Abstract behind a header so native tests can substitute an in-memory store.

`firmware/sensor-tag-wifi/include/config.h` *(create)*
Constants:

```cpp
constexpr const char* OTA_DEFAULT_HOST = "192.168.1.61";
constexpr uint8_t     OTA_BATTERY_FLOOR_PCT = 20;
constexpr uint32_t    OTA_HTTP_TIMEOUT_MS  = 30000;
```

URLs are built at runtime from `Preferences::getString("ota_host", OTA_DEFAULT_HOST)` + the compile-time `OTA_VARIANT` macro.

### New tests

`firmware/sensor-tag-wifi/test/test_ota_manifest/test_ota_manifest.cpp`
`firmware/sensor-tag-wifi/test/test_ota_decision/test_ota_decision.cpp`
`firmware/sensor-tag-wifi/test/test_ota_validation/test_ota_validation.cpp`
`firmware/sensor-tag-wifi/test/test_ota_sha256/test_ota_sha256.cpp`

### Modified files

`firmware/sensor-tag-wifi/partitions_tag.csv` — replace single 3 MB `app0` with dual 1.5 MB OTA slots:

```
nvs       data nvs    0x9000   0x5000
otadata   data ota    0xE000   0x2000
app0      app  ota_0  0x10000  0x180000
app1      app  ota_1  0x190000 0x180000
spiffs    data spiffs 0x310000 0xF0000
```

Total: 0x400000 (4 MB). Current binary is ~1.0 MB → 33% headroom per slot.

`firmware/sensor-tag-wifi/platformio.ini` — for both `xiao-c6-sht31` and `xiao-c6-ds18b20` envs:

- `extra_scripts = pre:get_version.py` — script writes a `-DFIRMWARE_VERSION=\"...\"` build flag from `git describe --tags --always --dirty`.
- Add `-DOTA_VARIANT=\"sht31\"` and `-DOTA_VARIANT=\"ds18b20\"` in respective env build flags.

`firmware/sensor-tag-wifi/get_version.py` *(new)* — PIO pre-script that injects `FIRMWARE_VERSION`.

`firmware/sensor-tag-wifi/src/main.cpp`:

- `Ota::validateOnBoot()` early in `setup()`.
- `Ota::onPublishSuccess()` immediately after a successful `mqtt.publish()` ack.
- `Ota::checkAndApply(batteryPct)` after publish, before `esp_deep_sleep_start()`.

`firmware/sensor-tag-wifi/src/serial_console.{h,cpp}` — extend to accept `set ota_host <ip>` command, persisted via `Preferences`.

`tools/provision_tag.py` — gain `--ota-host` flag that drives the same serial command.

## State machine + NVS schema

**NVS namespace:** `ota`

| Key | Type | Lifetime |
|---|---|---|
| `attempted` | string | Set just before `esp_restart` after `set_boot_partition`. Cleared after `mark_app_valid_cancel_rollback`. |
| `failed` | string | Set when `validateOnBoot` detects a rollback. Cleared after a different version successfully validates. |

**Order of operations during apply** (matters for power-loss safety):

```
1. fetch manifest, parse
2. apply gates (same/failed/battery)
3. esp_ota_begin → stream download → SHA-256 verify
4. esp_ota_end
5. esp_ota_set_boot_partition(inactive)
6. NVS.attempted = manifest.version
7. esp_restart()
```

Power loss between 5 and 6 is safe: device boots the new firmware on restore; NVS.attempted is empty, so `validateOnBoot` does nothing; the new firmware still must publish to mark itself valid.

**`validateOnBootAction` truth table:**

| `attempted` (NVS) | `firmwareVersion` (compiled) | `PENDING_VERIFY` | Action |
|---|---|---|---|
| empty | any | any | NoOp |
| `vX` | `vX` | true | NoOp (let `onPublishSuccess` clear) |
| `vX` | `vX` | false | ClearAttempted (we already validated; clean up) |
| `vX` | `vY` (≠ vX) | any | RecordFailed (rollback occurred) |

**`shouldApply` truth table:**

| `current` | `manifest` | `failed` | `battery` | Result |
|---|---|---|---|---|
| `vX` | `vX` | * | * | false (already up to date) |
| `vX` | `vY` | `vY` | * | false (known bad) |
| `vX` | `vY` | * | <20 | false (low battery) |
| `vX` | `vY` (≠ vX, ≠ failed) | * | ≥20 | true |

## Failure handling matrix

| Failure | Behavior | Side effect |
|---|---|---|
| Manifest GET fails (timeout / 404 / parse error) | Log, continue to sleep | None |
| `esp_ota_begin` fails | Log, continue | None |
| Download stream read fails | `esp_ota_abort`, log, continue | None — partial write discarded |
| SHA-256 mismatch | `esp_ota_abort`, log, continue | None — *don't* mark version `failed` (could be transient corruption) |
| Power loss during steps 1-4 | Old firmware still active on next boot | None |
| Power loss between 5 and 6 | New firmware boots, NVS empty; standard PENDING_VERIFY path | Slight risk of false validation if new firmware happens to publish ok despite a real bug — bounded by validation gate |
| New firmware can't publish | Watchdog/reset → bootloader rolls back | Old firmware records `failed = manifest.version`, skips that version henceforth |
| Manifest forever serves a SHA that no binary matches | Device GETs manifest, attempts download, aborts on mismatch, retries every wake | Operator-visible via nginx 200s + serial log; fix on server |

## Server side

**Filesystem on combsense-web LXC (192.168.1.61):**

```
/var/www/combsense-firmware/
└── sensor-tag-wifi/
    ├── sht31/
    │   ├── manifest.json
    │   └── v0.2.0/firmware.bin
    └── ds18b20/
        ├── manifest.json
        └── v0.2.0/firmware.bin
```

Versioned subdirs preserve old binaries → instant rollback by editing `manifest.json` to point at an older `firmware.bin`. Old dirs prunable manually.

**Manifest shape:**

```json
{
  "version": "v0.2.0",
  "url": "http://192.168.1.61/firmware/sensor-tag-wifi/ds18b20/v0.2.0/firmware.bin",
  "sha256": "abc123…",
  "size": 1046912
}
```

**nginx fragment** — added to the `:80` server in `deploy/web/nginx/combsense-web.conf`, *before* the catch-all HTTPS redirect:

```nginx
location /firmware/ {
    alias /var/www/combsense-firmware/;
    autoindex off;
    default_type application/octet-stream;
    types { application/json json; application/octet-stream bin; }

    allow 192.168.0.0/16;
    deny all;
}
```

HTTP, not HTTPS: integrity is the SHA-256 in the manifest. The `allow` block is defense-in-depth against accidental WAN exposure.

**Publish workflow — `deploy/web/publish-firmware.sh`** (new):

Runs from the developer machine (not the LXC). Must be invoked from the repo root or chdir into `firmware/sensor-tag-wifi/`. Requires SSH key auth to `natas@192.168.1.61` with passwordless sudo (already established).

```
publish-firmware.sh <variant>
  1. VERSION=$(git describe --tags --always)
  2. pio run -e xiao-c6-<variant>
  3. SHA + SIZE of .pio/build/xiao-c6-<variant>/firmware.bin
  4. scp bin → /tmp/firmware.bin on LXC
  5. ssh: sudo mv into /var/www/combsense-firmware/sensor-tag-wifi/<variant>/<version>/firmware.bin
  6. write manifest.json locally with version/url/sha256/size
  7. scp manifest → /tmp; ssh: sudo mv into /var/www/combsense-firmware/sensor-tag-wifi/<variant>/manifest.json
```

Atomic via `mv` into final paths; never a partial-state window where the manifest references a missing binary.

**Cleanup policy:** old versioned subdirs are *not* auto-pruned. Manual housekeeping when storage matters (e.g., `rm -rf v0.1.x`). Acceptable for the expected publish cadence.

**One-time LXC setup (in implementation plan, not the script):**

- `mkdir -p /var/www/combsense-firmware/sensor-tag-wifi/{sht31,ds18b20}` (root-owned, world-readable)
- Add nginx fragment to `combsense-web.conf`, `nginx -t`, `systemctl reload nginx`

## Auth and integrity

| Concern | Mitigation |
|---|---|
| Bit-flip during download | SHA-256 in manifest, verified before `esp_ota_set_boot_partition` |
| Accidental WAN exposure | `allow 192.168.0.0/16; deny all;` in nginx |
| LAN-attached attacker injecting binaries | Out of scope for v1; documented as a follow-up if firmware host is ever exposed beyond LAN |
| Server compromise | Out of scope (root on the LXC = bigger problems than OTA) |

No TLS — without cert pinning + a CA bundle on the device, HTTPS would just be skip-verify TLS, which is pretend-security. Skip the complexity.

## Testing

### Native unit tests (`pio test -e native`, Unity)

| Test | Coverage |
|---|---|
| `test_ota_manifest` | Valid JSON, missing fields, malformed JSON, oversize input → struct fields populate or `false` |
| `test_ota_decision` | All paths through `shouldApply`: same version, failed version, low battery, valid update |
| `test_ota_validation` | All 4 rows of the `validateOnBootAction` truth table |
| `test_ota_sha256` | Streaming SHA-256 verifier produces known fixture hash |

All decision logic is in pure functions → 100% native coverage of the state machine without hardware.

### Hardware smoke tests (manual, post-deploy)

| Scenario | How to provoke | Pass condition |
|---|---|---|
| Happy-path upgrade | Bump tag, run `publish-firmware.sh ds18b20` | New version appears in Influx at next wake; serial log shows `download → reboot → validate` |
| Same-version no-op | Republish same tag | Only manifest GETs in nginx access log; no download |
| SHA-256 mismatch | Corrupt 1 byte of `firmware.bin` on LXC | Device aborts, continues publishing; no boot-partition change |
| Failed-publish rollback | Push a binary that hard-fails `mqtt.publish()` | One failed boot, bootloader rolls back, NVS records `failed`, subsequent wakes skip |
| Battery floor | Set `OTA_BATTERY_FLOOR_PCT=99` in a test build | Skip download even though manifest version differs |

### Diagnostic logging

Every OTA decision emits a single line over USB-CDC serial:

```
[OTA] check version=v0.2.0 manifest=v0.2.0 failed= → skip(same)
[OTA] check version=v0.2.0 manifest=v0.3.0 failed= battery=85 → download
[OTA] download bytes=1046912 sha256_ok=true → reboot
[OTA] validate result=rollback_detected attempted=v0.3.0
```

Smoke tests become self-documenting — tail serial during the test, paste lines into the runbook.

## Open questions

None. All design decisions resolved during brainstorming.

## Future work (out of scope for v1)

- HMAC or signature on the manifest (LAN-attacker mitigation).
- Per-device staged rollouts (canary subset before full rollout).
- Apply same OTA pattern to the original BLE-only `firmware/sensor-tag/`.
- Server-side cleanup of old versioned dirs (currently manual).
