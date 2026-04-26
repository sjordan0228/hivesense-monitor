# Sensor-Tag-Wifi OTA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the home-yard `sensor-tag-wifi` (XIAO ESP32-C6) HTTP-pull OTA so a device deployed at a beehive can be updated without physical access.

**Architecture:** Per-wake flow — after publishing the reading, fetch a JSON manifest from a local nginx host, compare to compiled-in `FIRMWARE_VERSION`, stream-download the binary into the inactive OTA slot with running SHA-256, verify, set boot partition, and reboot. New firmware proves itself by completing one publish; otherwise the bootloader rolls back. All decision logic lives in pure functions with native Unity tests.

**Tech Stack:**
- Firmware: ESP-IDF `esp_ota_*` + `esp_http_client_*`, Arduino `Preferences`, mbedtls SHA-256
- Build: PlatformIO `pioarduino` (53.03.10), `extra_scripts = pre:get_version.py`
- Tests: Unity native (`pio test -e native`), `picosha2.h` for native SHA-256
- Server: nginx fragment on `combsense-web` LXC (192.168.1.61), bash publish script
- Provisioning: extension to existing `serial_console` + `tools/provision_tag.py`

**Spec:** `docs/superpowers/specs/2026-04-24-sensor-tag-wifi-ota-design.md` (committed at `399db5e`).

---

## File Structure

### New firmware files
- `firmware/sensor-tag-wifi/include/ota.h` — public `Ota::` API
- `firmware/sensor-tag-wifi/src/ota.cpp` — side-effecting shell (HTTP + `esp_ota_*`)
- `firmware/sensor-tag-wifi/include/ota_manifest.h` — `Manifest` struct + parser declaration
- `firmware/sensor-tag-wifi/src/ota_manifest.cpp` — pure JSON parser
- `firmware/sensor-tag-wifi/include/ota_decision.h` — pure decision functions
- `firmware/sensor-tag-wifi/src/ota_decision.cpp` — pure decision functions
- `firmware/sensor-tag-wifi/include/ota_state.h` — Preferences adapter interface
- `firmware/sensor-tag-wifi/src/ota_state.cpp` — Preferences adapter (device + native shim)
- `firmware/sensor-tag-wifi/include/ota_sha256.h` — streaming SHA-256 wrapper
- `firmware/sensor-tag-wifi/src/ota_sha256.cpp` — mbedtls (device) / picosha2 (native) backend
- `firmware/sensor-tag-wifi/include/picosha2.h` — vendored single-header SHA-256 (MIT)
- `firmware/sensor-tag-wifi/get_version.py` — PIO pre-script

### New test files (each in its own dir per PIO Unity convention)
- `firmware/sensor-tag-wifi/test/test_ota_manifest/test_ota_manifest.cpp`
- `firmware/sensor-tag-wifi/test/test_ota_decision/test_ota_decision.cpp`
- `firmware/sensor-tag-wifi/test/test_ota_validation/test_ota_validation.cpp`
- `firmware/sensor-tag-wifi/test/test_ota_sha256/test_ota_sha256.cpp`

### Modified firmware files
- `firmware/sensor-tag-wifi/include/config.h` — add OTA constants
- `firmware/sensor-tag-wifi/partitions_tag.csv` — single 3 MB app → dual 1.5 MB OTA slots
- `firmware/sensor-tag-wifi/platformio.ini` — `extra_scripts`, `OTA_VARIANT`, native `build_src_filter`
- `firmware/sensor-tag-wifi/src/main.cpp` — wire `Ota::validateOnBoot` / `onPublishSuccess` / `checkAndApply`
- `firmware/sensor-tag-wifi/src/serial_console.cpp` — add `ota_host` to known keys

### New / modified deploy + tooling files
- `deploy/web/nginx/combsense-web.conf` — add `/firmware/` location (HTTP `:80`)
- `deploy/web/publish-firmware.sh` — new build+upload+manifest script
- `tools/provision_tag.py` — `--ota-host` flag

---

## Task ordering rationale

1. **Tasks 1–4**: pure-function modules first, fully native-testable. Each is a complete TDD cycle.
2. **Task 5**: build system wiring (version + variant + native env additions). Required before any device build.
3. **Task 6**: dual-slot partition table.
4. **Tasks 7–8**: side-effecting shell layered on top of the pure functions.
5. **Task 9**: integrate into `main.cpp`. Device build must succeed end-to-end here.
6. **Tasks 10–11**: provisioning ergonomics for `ota_host`.
7. **Tasks 12–13**: server side (LXC nginx + publish script). Independent of firmware tasks; can be done in parallel by an operator but plan keeps them sequential for review simplicity.
8. **Tasks 14–15**: bootstrap + hardware smoke tests.

---

## Task 1: ota_manifest — pure JSON parser

**Files:**
- Create: `firmware/sensor-tag-wifi/include/ota_manifest.h`
- Create: `firmware/sensor-tag-wifi/src/ota_manifest.cpp`
- Create: `firmware/sensor-tag-wifi/test/test_ota_manifest/test_ota_manifest.cpp`
- Modify: `firmware/sensor-tag-wifi/platformio.ini` (native env `build_src_filter`)

- [ ] **Step 1: Write the header**

`firmware/sensor-tag-wifi/include/ota_manifest.h`:
```cpp
#pragma once

#include <cstddef>

struct Manifest {
    char version[32];
    char url[256];
    char sha256[65];   // 64 hex chars + null
    size_t size;
};

bool parseManifest(const char* json, size_t len, Manifest& out);
```

- [ ] **Step 2: Write the failing tests**

`firmware/sensor-tag-wifi/test/test_ota_manifest/test_ota_manifest.cpp`:
```cpp
#include <unity.h>
#include <cstring>
#include "ota_manifest.h"

void setUp() {}
void tearDown() {}

void test_parses_valid_manifest() {
    const char* j =
        "{\"version\":\"v0.2.0\","
        "\"url\":\"http://192.168.1.61/firmware/sensor-tag-wifi/sht31/v0.2.0/firmware.bin\","
        "\"sha256\":\"abc123def456abc123def456abc123def456abc123def456abc123def4561234\","
        "\"size\":1046912}";
    Manifest m {};
    TEST_ASSERT_TRUE(parseManifest(j, strlen(j), m));
    TEST_ASSERT_EQUAL_STRING("v0.2.0", m.version);
    TEST_ASSERT_EQUAL_STRING(
        "http://192.168.1.61/firmware/sensor-tag-wifi/sht31/v0.2.0/firmware.bin", m.url);
    TEST_ASSERT_EQUAL_STRING(
        "abc123def456abc123def456abc123def456abc123def456abc123def4561234", m.sha256);
    TEST_ASSERT_EQUAL_UINT32(1046912, m.size);
}

void test_rejects_missing_field() {
    const char* j = "{\"version\":\"v0.2.0\",\"url\":\"http://x/y\",\"size\":100}";
    Manifest m {};
    TEST_ASSERT_FALSE(parseManifest(j, strlen(j), m));
}

void test_rejects_malformed_json() {
    const char* j = "{not valid json";
    Manifest m {};
    TEST_ASSERT_FALSE(parseManifest(j, strlen(j), m));
}

void test_rejects_oversize_url() {
    char j[600];
    char url[300];
    memset(url, 'a', sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    snprintf(j, sizeof(j),
        "{\"version\":\"v1\",\"url\":\"%s\",\"sha256\":\"%s\",\"size\":1}",
        url,
        "0000000000000000000000000000000000000000000000000000000000000000");
    Manifest m {};
    TEST_ASSERT_FALSE(parseManifest(j, strlen(j), m));
}

void test_rejects_wrong_sha_length() {
    const char* j =
        "{\"version\":\"v1\",\"url\":\"http://x/y\","
        "\"sha256\":\"deadbeef\",\"size\":1}";
    Manifest m {};
    TEST_ASSERT_FALSE(parseManifest(j, strlen(j), m));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_valid_manifest);
    RUN_TEST(test_rejects_missing_field);
    RUN_TEST(test_rejects_malformed_json);
    RUN_TEST(test_rejects_oversize_url);
    RUN_TEST(test_rejects_wrong_sha_length);
    return UNITY_END();
}
```

- [ ] **Step 3: Add ota_manifest.cpp to native build_src_filter**

Edit `firmware/sensor-tag-wifi/platformio.ini`, replace the `[env:native]` block's `build_src_filter` with:
```ini
build_src_filter =
    +<payload.cpp>
    +<ota_manifest.cpp>
```

- [ ] **Step 4: Run the test, verify it fails**

```bash
cd firmware/sensor-tag-wifi
pio test -e native -f test_ota_manifest
```

Expected: FAIL — link error (`parseManifest` undefined) or compile error.

- [ ] **Step 5: Write the minimal implementation**

`firmware/sensor-tag-wifi/src/ota_manifest.cpp`:
```cpp
#include "ota_manifest.h"

#include <cstring>
#include <cstdlib>
#include <cctype>

namespace {

const char* findKey(const char* json, size_t len, const char* key) {
    char needle[40];
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || (size_t)n >= sizeof(needle)) return nullptr;
    const char* end = json + len;
    for (const char* p = json; p + n <= end; p++) {
        if (memcmp(p, needle, n) == 0) return p + n;
    }
    return nullptr;
}

bool extractString(const char* json, size_t len, const char* key,
                   char* out, size_t outCap) {
    const char* p = findKey(json, len, key);
    if (!p) return false;
    const char* end = json + len;
    while (p < end && *p != ':') p++;
    if (p == end) return false;
    p++;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p == end || *p != '"') return false;
    p++;
    const char* start = p;
    while (p < end && *p != '"') p++;
    if (p == end) return false;
    size_t length = (size_t)(p - start);
    if (length + 1 > outCap) return false;
    memcpy(out, start, length);
    out[length] = '\0';
    return true;
}

bool extractNumber(const char* json, size_t len, const char* key, size_t& out) {
    const char* p = findKey(json, len, key);
    if (!p) return false;
    const char* end = json + len;
    while (p < end && *p != ':') p++;
    if (p == end) return false;
    p++;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p == end || !isdigit((unsigned char)*p)) return false;
    char buf[24] = {};
    size_t i = 0;
    while (p < end && isdigit((unsigned char)*p) && i < sizeof(buf) - 1) {
        buf[i++] = *p++;
    }
    out = (size_t)strtoul(buf, nullptr, 10);
    return true;
}

}  // namespace

bool parseManifest(const char* json, size_t len, Manifest& out) {
    if (!json || len == 0) return false;
    if (!extractString(json, len, "version", out.version, sizeof(out.version))) return false;
    if (!extractString(json, len, "url",     out.url,     sizeof(out.url)))     return false;
    if (!extractString(json, len, "sha256",  out.sha256,  sizeof(out.sha256)))  return false;
    if (strlen(out.sha256) != 64) return false;
    if (!extractNumber(json, len, "size", out.size)) return false;
    return true;
}
```

- [ ] **Step 6: Run the tests, verify they pass**

```bash
cd firmware/sensor-tag-wifi
pio test -e native -f test_ota_manifest
```

Expected: PASS — 5 tests passing.

- [ ] **Step 7: Commit**

```bash
git add firmware/sensor-tag-wifi/include/ota_manifest.h \
        firmware/sensor-tag-wifi/src/ota_manifest.cpp \
        firmware/sensor-tag-wifi/test/test_ota_manifest/test_ota_manifest.cpp \
        firmware/sensor-tag-wifi/platformio.ini
git commit -m "feat(sensor-tag-wifi): manifest JSON parser with native tests"
```

---

## Task 2: ota_decision — pure decision functions

**Files:**
- Create: `firmware/sensor-tag-wifi/include/ota_decision.h`
- Create: `firmware/sensor-tag-wifi/src/ota_decision.cpp`
- Create: `firmware/sensor-tag-wifi/test/test_ota_decision/test_ota_decision.cpp`
- Create: `firmware/sensor-tag-wifi/test/test_ota_validation/test_ota_validation.cpp`
- Modify: `firmware/sensor-tag-wifi/platformio.ini` (native `build_src_filter`)

- [ ] **Step 1: Write the header**

`firmware/sensor-tag-wifi/include/ota_decision.h`:
```cpp
#pragma once

#include <cstdint>

bool shouldApply(const char* current,
                 const char* manifest,
                 const char* failed,
                 uint8_t batteryPct);

enum class ValidateAction {
    NoOp,
    ClearAttempted,
    RecordFailed,
};

ValidateAction validateOnBootAction(const char* firmwareVersion,
                                    const char* attempted,
                                    bool isPendingVerify);
```

- [ ] **Step 2: Write the failing tests for shouldApply**

`firmware/sensor-tag-wifi/test/test_ota_decision/test_ota_decision.cpp`:
```cpp
#include <unity.h>
#include "ota_decision.h"

void setUp() {}
void tearDown() {}

void test_skip_when_versions_match() {
    TEST_ASSERT_FALSE(shouldApply("v0.2.0", "v0.2.0", "", 100));
}

void test_skip_when_manifest_matches_failed() {
    TEST_ASSERT_FALSE(shouldApply("v0.2.0", "v0.3.0", "v0.3.0", 100));
}

void test_skip_when_battery_below_floor() {
    TEST_ASSERT_FALSE(shouldApply("v0.2.0", "v0.3.0", "", 19));
}

void test_apply_when_new_version_and_healthy_battery() {
    TEST_ASSERT_TRUE(shouldApply("v0.2.0", "v0.3.0", "", 20));
    TEST_ASSERT_TRUE(shouldApply("v0.2.0", "v0.3.0", "", 100));
}

void test_apply_when_failed_is_different_version() {
    TEST_ASSERT_TRUE(shouldApply("v0.2.0", "v0.3.0", "v0.2.5", 80));
}

void test_skip_when_manifest_empty() {
    TEST_ASSERT_FALSE(shouldApply("v0.2.0", "", "", 100));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_skip_when_versions_match);
    RUN_TEST(test_skip_when_manifest_matches_failed);
    RUN_TEST(test_skip_when_battery_below_floor);
    RUN_TEST(test_apply_when_new_version_and_healthy_battery);
    RUN_TEST(test_apply_when_failed_is_different_version);
    RUN_TEST(test_skip_when_manifest_empty);
    return UNITY_END();
}
```

- [ ] **Step 3: Write the failing tests for validateOnBootAction**

`firmware/sensor-tag-wifi/test/test_ota_validation/test_ota_validation.cpp`:
```cpp
#include <unity.h>
#include "ota_decision.h"

void setUp() {}
void tearDown() {}

void test_noop_when_attempted_empty() {
    TEST_ASSERT_TRUE(validateOnBootAction("v0.2.0", "", false) == ValidateAction::NoOp);
    TEST_ASSERT_TRUE(validateOnBootAction("v0.2.0", "", true)  == ValidateAction::NoOp);
}

void test_noop_when_pending_and_versions_match() {
    TEST_ASSERT_TRUE(
        validateOnBootAction("v0.3.0", "v0.3.0", true) == ValidateAction::NoOp);
}

void test_clear_attempted_when_already_validated() {
    TEST_ASSERT_TRUE(
        validateOnBootAction("v0.3.0", "v0.3.0", false) == ValidateAction::ClearAttempted);
}

void test_record_failed_when_running_old_firmware_after_attempt() {
    TEST_ASSERT_TRUE(
        validateOnBootAction("v0.2.0", "v0.3.0", false) == ValidateAction::RecordFailed);
    TEST_ASSERT_TRUE(
        validateOnBootAction("v0.2.0", "v0.3.0", true)  == ValidateAction::RecordFailed);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_noop_when_attempted_empty);
    RUN_TEST(test_noop_when_pending_and_versions_match);
    RUN_TEST(test_clear_attempted_when_already_validated);
    RUN_TEST(test_record_failed_when_running_old_firmware_after_attempt);
    return UNITY_END();
}
```

- [ ] **Step 4: Add ota_decision.cpp to native build_src_filter**

Edit `[env:native]` `build_src_filter` to:
```ini
build_src_filter =
    +<payload.cpp>
    +<ota_manifest.cpp>
    +<ota_decision.cpp>
```

- [ ] **Step 5: Run tests, verify they fail**

```bash
cd firmware/sensor-tag-wifi
pio test -e native -f test_ota_decision -f test_ota_validation
```

Expected: FAIL — link errors.

- [ ] **Step 6: Write the implementation**

`firmware/sensor-tag-wifi/src/ota_decision.cpp`:
```cpp
#include "ota_decision.h"

#include <cstring>

namespace {
constexpr uint8_t BATTERY_FLOOR_PCT = 20;

bool isEmpty(const char* s) { return s == nullptr || s[0] == '\0'; }
}  // namespace

bool shouldApply(const char* current,
                 const char* manifest,
                 const char* failed,
                 uint8_t batteryPct) {
    if (isEmpty(manifest)) return false;
    if (!isEmpty(current) && strcmp(current, manifest) == 0) return false;
    if (!isEmpty(failed) && strcmp(failed, manifest) == 0) return false;
    if (batteryPct < BATTERY_FLOOR_PCT) return false;
    return true;
}

ValidateAction validateOnBootAction(const char* firmwareVersion,
                                    const char* attempted,
                                    bool isPendingVerify) {
    if (isEmpty(attempted)) return ValidateAction::NoOp;
    if (strcmp(firmwareVersion, attempted) == 0) {
        return isPendingVerify ? ValidateAction::NoOp : ValidateAction::ClearAttempted;
    }
    return ValidateAction::RecordFailed;
}
```

- [ ] **Step 7: Run tests, verify they pass**

```bash
cd firmware/sensor-tag-wifi
pio test -e native -f test_ota_decision -f test_ota_validation
```

Expected: PASS — 6 + 4 = 10 tests passing.

- [ ] **Step 8: Commit**

```bash
git add firmware/sensor-tag-wifi/include/ota_decision.h \
        firmware/sensor-tag-wifi/src/ota_decision.cpp \
        firmware/sensor-tag-wifi/test/test_ota_decision/test_ota_decision.cpp \
        firmware/sensor-tag-wifi/test/test_ota_validation/test_ota_validation.cpp \
        firmware/sensor-tag-wifi/platformio.ini
git commit -m "feat(sensor-tag-wifi): pure OTA decision + boot-validation logic"
```

---

## Task 3: ota_sha256 — streaming SHA-256 verifier

**Files:**
- Create: `firmware/sensor-tag-wifi/include/picosha2.h` (vendored)
- Create: `firmware/sensor-tag-wifi/include/ota_sha256.h`
- Create: `firmware/sensor-tag-wifi/src/ota_sha256.cpp`
- Create: `firmware/sensor-tag-wifi/test/test_ota_sha256/test_ota_sha256.cpp`
- Modify: `firmware/sensor-tag-wifi/platformio.ini`

- [ ] **Step 1: Vendor picosha2.h**

Download `https://raw.githubusercontent.com/okdshin/PicoSHA2/27fcf6979298949e8a462e16d09a0351c18fcaf2/picosha2.h` and save it as `firmware/sensor-tag-wifi/include/picosha2.h`. Single-header SHA-256, MIT license — used only when building for `native` (mbedtls is unavailable off-target).

```bash
curl -fsSL "https://raw.githubusercontent.com/okdshin/PicoSHA2/27fcf6979298949e8a462e16d09a0351c18fcaf2/picosha2.h" \
    -o firmware/sensor-tag-wifi/include/picosha2.h
```

Verify the file is ~140 lines and starts with the `#ifndef PICOSHA2_H` guard.

- [ ] **Step 2: Write the header**

`firmware/sensor-tag-wifi/include/ota_sha256.h`:
```cpp
#pragma once

#include <cstddef>
#include <cstdint>

class Sha256Streamer {
public:
    Sha256Streamer();
    ~Sha256Streamer();

    void reset();
    void update(const uint8_t* data, size_t len);
    void finalizeToHex(char outHex[65]);   // writes 64 lowercase hex + null

    bool matches(const char* expectedHex);  // resets internally; call after finalizeToHex

private:
    void* impl_;
    char lastHex_[65];
};
```

- [ ] **Step 3: Write the failing test**

`firmware/sensor-tag-wifi/test/test_ota_sha256/test_ota_sha256.cpp`:
```cpp
#include <unity.h>
#include <cstring>
#include "ota_sha256.h"

void setUp() {}
void tearDown() {}

// Known SHA-256 fixtures
// "" -> e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
// "abc" -> ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad

void test_empty_input_hash() {
    Sha256Streamer s;
    char hex[65] = {};
    s.finalizeToHex(hex);
    TEST_ASSERT_EQUAL_STRING(
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", hex);
}

void test_single_chunk_abc() {
    Sha256Streamer s;
    s.update(reinterpret_cast<const uint8_t*>("abc"), 3);
    char hex[65] = {};
    s.finalizeToHex(hex);
    TEST_ASSERT_EQUAL_STRING(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
}

void test_streaming_matches_single_chunk() {
    Sha256Streamer s;
    s.update(reinterpret_cast<const uint8_t*>("a"), 1);
    s.update(reinterpret_cast<const uint8_t*>("b"), 1);
    s.update(reinterpret_cast<const uint8_t*>("c"), 1);
    char hex[65] = {};
    s.finalizeToHex(hex);
    TEST_ASSERT_EQUAL_STRING(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
}

void test_matches_compares_case_insensitive() {
    Sha256Streamer s;
    s.update(reinterpret_cast<const uint8_t*>("abc"), 3);
    char hex[65] = {};
    s.finalizeToHex(hex);
    TEST_ASSERT_TRUE(s.matches(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    TEST_ASSERT_TRUE(s.matches(
        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
    TEST_ASSERT_FALSE(s.matches("0000000000000000000000000000000000000000000000000000000000000000"));
}

void test_reset_allows_reuse() {
    Sha256Streamer s;
    s.update(reinterpret_cast<const uint8_t*>("ignored"), 7);
    s.reset();
    s.update(reinterpret_cast<const uint8_t*>("abc"), 3);
    char hex[65] = {};
    s.finalizeToHex(hex);
    TEST_ASSERT_EQUAL_STRING(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_input_hash);
    RUN_TEST(test_single_chunk_abc);
    RUN_TEST(test_streaming_matches_single_chunk);
    RUN_TEST(test_matches_compares_case_insensitive);
    RUN_TEST(test_reset_allows_reuse);
    return UNITY_END();
}
```

- [ ] **Step 4: Add ota_sha256.cpp to native build_src_filter**

Edit `[env:native]` `build_src_filter` to:
```ini
build_src_filter =
    +<payload.cpp>
    +<ota_manifest.cpp>
    +<ota_decision.cpp>
    +<ota_sha256.cpp>
```

- [ ] **Step 5: Run, verify failure**

```bash
cd firmware/sensor-tag-wifi
pio test -e native -f test_ota_sha256
```

Expected: FAIL — link error.

- [ ] **Step 6: Write the implementation**

`firmware/sensor-tag-wifi/src/ota_sha256.cpp`:
```cpp
#include "ota_sha256.h"

#include <cstring>
#include <cctype>

#ifdef ESP_PLATFORM
#include "mbedtls/sha256.h"
struct Backend {
    mbedtls_sha256_context ctx;
    Backend() { mbedtls_sha256_init(&ctx); mbedtls_sha256_starts(&ctx, 0); }
    ~Backend() { mbedtls_sha256_free(&ctx); }
    void reset() {
        mbedtls_sha256_free(&ctx);
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
    }
    void update(const uint8_t* d, size_t n) { mbedtls_sha256_update(&ctx, d, n); }
    void finalize(uint8_t out[32]) { mbedtls_sha256_finish(&ctx, out); }
};
#else
#include "picosha2.h"
struct Backend {
    picosha2::hash256_one_by_one hasher;
    Backend() { hasher.init(); }
    void reset() { hasher.init(); }
    void update(const uint8_t* d, size_t n) { hasher.process(d, d + n); }
    void finalize(uint8_t out[32]) {
        hasher.finish();
        std::vector<unsigned char> v;
        hasher.get_hash_bytes(v);
        memcpy(out, v.data(), 32);
    }
};
#endif

namespace {
void toHex(const uint8_t in[32], char out[65]) {
    static const char* lut = "0123456789abcdef";
    for (size_t i = 0; i < 32; i++) {
        out[2 * i]     = lut[in[i] >> 4];
        out[2 * i + 1] = lut[in[i] & 0x0f];
    }
    out[64] = '\0';
}

bool hexEqIgnoreCase(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}
}  // namespace

Sha256Streamer::Sha256Streamer() : impl_(new Backend()) { lastHex_[0] = '\0'; }
Sha256Streamer::~Sha256Streamer() { delete static_cast<Backend*>(impl_); }

void Sha256Streamer::reset() { static_cast<Backend*>(impl_)->reset(); lastHex_[0] = '\0'; }
void Sha256Streamer::update(const uint8_t* data, size_t len) {
    static_cast<Backend*>(impl_)->update(data, len);
}
void Sha256Streamer::finalizeToHex(char outHex[65]) {
    uint8_t raw[32];
    static_cast<Backend*>(impl_)->finalize(raw);
    toHex(raw, outHex);
    memcpy(lastHex_, outHex, 65);
}
bool Sha256Streamer::matches(const char* expectedHex) {
    if (lastHex_[0] == '\0') return false;
    return hexEqIgnoreCase(lastHex_, expectedHex);
}
```

- [ ] **Step 7: Run tests, verify they pass**

```bash
cd firmware/sensor-tag-wifi
pio test -e native -f test_ota_sha256
```

Expected: PASS — 5 tests passing.

- [ ] **Step 8: Commit**

```bash
git add firmware/sensor-tag-wifi/include/picosha2.h \
        firmware/sensor-tag-wifi/include/ota_sha256.h \
        firmware/sensor-tag-wifi/src/ota_sha256.cpp \
        firmware/sensor-tag-wifi/test/test_ota_sha256/test_ota_sha256.cpp \
        firmware/sensor-tag-wifi/platformio.ini
git commit -m "feat(sensor-tag-wifi): streaming SHA-256 verifier (mbedtls/picosha2)"
```

---

## Task 4: ota_state — Preferences adapter

**Files:**
- Create: `firmware/sensor-tag-wifi/include/ota_state.h`
- Create: `firmware/sensor-tag-wifi/src/ota_state.cpp`

This file is **device-only** (uses Arduino `Preferences`), so no native test. The decision logic is already covered by `test_ota_decision`/`test_ota_validation`.

- [ ] **Step 1: Write the header**

`firmware/sensor-tag-wifi/include/ota_state.h`:
```cpp
#pragma once

namespace OtaState {
    void getAttempted(char* out, size_t outCap);
    void setAttempted(const char* version);
    void clearAttempted();

    void getFailed(char* out, size_t outCap);
    void setFailed(const char* version);
    void clearFailed();
}
```

- [ ] **Step 2: Write the implementation**

`firmware/sensor-tag-wifi/src/ota_state.cpp`:
```cpp
#include "ota_state.h"

#include <Preferences.h>
#include <cstring>

namespace {
constexpr const char* OTA_NS  = "ota";
constexpr const char* K_ATTEMPTED = "attempted";
constexpr const char* K_FAILED    = "failed";

void readKey(const char* key, char* out, size_t outCap) {
    Preferences p;
    p.begin(OTA_NS, true);
    String v = p.getString(key, "");
    p.end();
    size_t n = v.length() < outCap ? v.length() : outCap - 1;
    memcpy(out, v.c_str(), n);
    out[n] = '\0';
}

void writeKey(const char* key, const char* value) {
    Preferences p;
    p.begin(OTA_NS, false);
    p.putString(key, value);
    p.end();
}

void removeKey(const char* key) {
    Preferences p;
    p.begin(OTA_NS, false);
    p.remove(key);
    p.end();
}
}  // namespace

namespace OtaState {

void getAttempted(char* out, size_t outCap) { readKey(K_ATTEMPTED, out, outCap); }
void setAttempted(const char* v)            { writeKey(K_ATTEMPTED, v); }
void clearAttempted()                       { removeKey(K_ATTEMPTED); }

void getFailed(char* out, size_t outCap)    { readKey(K_FAILED, out, outCap); }
void setFailed(const char* v)               { writeKey(K_FAILED, v); }
void clearFailed()                          { removeKey(K_FAILED); }

}  // namespace OtaState
```

- [ ] **Step 3: Verify it compiles in the device build (full build deferred to Task 9)**

This file alone won't link until `main.cpp` references it; Task 5/9 will pull it in. For now, syntax-check only by ensuring no unresolved includes.

- [ ] **Step 4: Commit**

```bash
git add firmware/sensor-tag-wifi/include/ota_state.h \
        firmware/sensor-tag-wifi/src/ota_state.cpp
git commit -m "feat(sensor-tag-wifi): NVS-backed OTA state adapter"
```

---

## Task 5: Build-time version + variant injection

**Files:**
- Create: `firmware/sensor-tag-wifi/get_version.py`
- Modify: `firmware/sensor-tag-wifi/platformio.ini`

- [ ] **Step 1: Write the PIO pre-script**

`firmware/sensor-tag-wifi/get_version.py`:
```python
"""Inject FIRMWARE_VERSION as a build flag from `git describe`.

Runs as a PlatformIO pre-script (extra_scripts = pre:get_version.py). Must
be deterministic for cache hits — same git state must yield the same flag.
"""
import subprocess

Import("env")  # noqa: F821 — provided by PlatformIO


def _git_describe() -> str:
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL,
        )
        return out.decode("utf-8").strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


version = _git_describe()
print(f"[get_version] FIRMWARE_VERSION={version}")
env.Append(BUILD_FLAGS=[f'-DFIRMWARE_VERSION=\\"{version}\\"'])  # noqa: F821
```

- [ ] **Step 2: Wire pre-script + OTA_VARIANT into platformio.ini**

Edit `firmware/sensor-tag-wifi/platformio.ini`. In `[env:xiao-c6-sht31]`, add `extra_scripts` and `OTA_VARIANT`:

```ini
[env:xiao-c6-sht31]
extends = env:esp32-base
board = esp32-c6-devkitm-1
extra_scripts = pre:get_version.py
lib_deps =
    adafruit/Adafruit SHT31 Library@^2.2.2
    knolleary/PubSubClient@^2.8
    Networking
build_flags =
    ${env:esp32-base.build_flags}
    -I../shared
    -DCORE_DEBUG_LEVEL=3
    -DSENSOR_SHT31
    -DOTA_VARIANT=\"sht31\"
build_src_filter =
    +<*>
    -<sensor_ds18b20.cpp>
```

Same edit to `[env:xiao-c6-ds18b20]`:

```ini
[env:xiao-c6-ds18b20]
extends = env:esp32-base
board = esp32-c6-devkitm-1
extra_scripts = pre:get_version.py
lib_deps =
    pstolarz/OneWireNg@^0.14.1
    milesburton/DallasTemperature@^3.11.0
    knolleary/PubSubClient@^2.8
    Networking
lib_ignore = OneWire
build_flags =
    ${env:esp32-base.build_flags}
    -I../shared
    -DCORE_DEBUG_LEVEL=3
    -DSENSOR_DS18B20
    -DOTA_VARIANT=\"ds18b20\"
build_src_filter =
    +<*>
    -<sensor_sht31.cpp>
```

- [ ] **Step 3: Verify pre-script fires**

```bash
cd firmware/sensor-tag-wifi
pio run -e xiao-c6-sht31 -t clean
pio run -e xiao-c6-sht31 2>&1 | grep "FIRMWARE_VERSION="
```

Expected: a line like `[get_version] FIRMWARE_VERSION=v0.x.y-...-dirty`. Build will fail later because `Ota::` is not yet referenced — that's fine; we only verify the script ran.

If the build fails for *other* reasons (existing code regression), stop and investigate; the build was passing before this task.

- [ ] **Step 4: Commit**

```bash
git add firmware/sensor-tag-wifi/get_version.py \
        firmware/sensor-tag-wifi/platformio.ini
git commit -m "build(sensor-tag-wifi): inject FIRMWARE_VERSION + OTA_VARIANT at build time"
```

---

## Task 6: Dual-slot partition table

**Files:**
- Modify: `firmware/sensor-tag-wifi/partitions_tag.csv`

- [ ] **Step 1: Replace the partition table**

Replace the entire contents of `firmware/sensor-tag-wifi/partitions_tag.csv` with:

```
# Name,     Type, SubType, Offset,   Size,     Flags
nvs,        data, nvs,     0x9000,   0x5000,
otadata,    data, ota,     0xE000,   0x2000,
app0,       app,  ota_0,   0x10000,  0x180000,
app1,       app,  ota_1,   0x190000, 0x180000,
spiffs,     data, spiffs,  0x310000, 0xF0000,
```

Total: 0x400000 (4 MB). Each app slot is 0x180000 (1.5 MB) — current binary is ~1 MB → 33% headroom.

- [ ] **Step 2: Verify the device build still links**

```bash
cd firmware/sensor-tag-wifi
pio run -e xiao-c6-sht31 -t clean
pio run -e xiao-c6-sht31 2>&1 | tail -30
```

Expected: build succeeds with new partition layout. Look for `RAM:` and `Flash:` lines in the output and confirm `Flash` size used is well under `0x180000` (1.5 MB).

If linker reports `region 'iram0_2_seg' overflowed` or similar, we've miscalculated — stop and revisit. The current binary is ~1 MB, well under 1.5 MB, so this should not happen.

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/partitions_tag.csv
git commit -m "build(sensor-tag-wifi): dual 1.5MB OTA partitions for HTTP-pull updates"
```

---

## Task 7: Extend config.h with OTA constants

**Files:**
- Modify: `firmware/sensor-tag-wifi/include/config.h`

- [ ] **Step 1: Append OTA constants**

Open `firmware/sensor-tag-wifi/include/config.h` and append after the `Payload` section:

```cpp
// =============================================================================
// OTA
// =============================================================================

constexpr const char* OTA_DEFAULT_HOST     = "192.168.1.61";
constexpr uint8_t     OTA_BATTERY_FLOOR_PCT = 20;
constexpr uint32_t    OTA_HTTP_TIMEOUT_MS  = 30000;
constexpr const char* NVS_KEY_OTA_HOST     = "ota_host";
```

- [ ] **Step 2: Verify build**

```bash
cd firmware/sensor-tag-wifi
pio run -e xiao-c6-sht31 2>&1 | tail -5
```

Expected: build succeeds (no behavioural change yet).

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/include/config.h
git commit -m "feat(sensor-tag-wifi): config constants for OTA host + battery floor"
```

---

## Task 8: ota.cpp — HTTP + esp_ota_* shell

**Files:**
- Create: `firmware/sensor-tag-wifi/include/ota.h`
- Create: `firmware/sensor-tag-wifi/src/ota.cpp`

This file is the side-effecting glue. All branching is delegated to the pure functions from Tasks 1–3, so device-only and not native-tested. Pattern mirrors `firmware/collector/src/ota_self.cpp` for the streaming download.

- [ ] **Step 1: Write the header**

`firmware/sensor-tag-wifi/include/ota.h`:
```cpp
#pragma once

#include <cstdint>

namespace Ota {
    void validateOnBoot();
    void onPublishSuccess();
    void checkAndApply(uint8_t batteryPct);
}
```

- [ ] **Step 2: Write the implementation**

`firmware/sensor-tag-wifi/src/ota.cpp`:
```cpp
#include "ota.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <cstring>

#include "config.h"
#include "ota_decision.h"
#include "ota_manifest.h"
#include "ota_sha256.h"
#include "ota_state.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

#ifndef OTA_VARIANT
#define OTA_VARIANT "unknown"
#endif

namespace {

void readOtaHost(char* out, size_t outCap) {
    Preferences p;
    p.begin(NVS_NAMESPACE, true);
    String v = p.getString(NVS_KEY_OTA_HOST, OTA_DEFAULT_HOST);
    p.end();
    size_t n = v.length() < outCap ? v.length() : outCap - 1;
    memcpy(out, v.c_str(), n);
    out[n] = '\0';
}

bool fetchManifestText(const char* url, char* buf, size_t bufCap, int& outLen) {
    esp_http_client_config_t cfg = {};
    cfg.url        = url;
    cfg.timeout_ms = OTA_HTTP_TIMEOUT_MS;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int total = 0;
        while (total < (int)bufCap - 1) {
            int n = esp_http_client_read(client, buf + total, (int)bufCap - 1 - total);
            if (n < 0) { total = -1; break; }
            if (n == 0) break;
            total += n;
        }
        if (total >= 0) {
            buf[total] = '\0';
            outLen = total;
            ok = (esp_http_client_get_status_code(client) == 200);
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    return ok;
}

bool downloadAndStream(const Manifest& m, const esp_partition_t* target) {
    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
        Serial.println("[OTA] esp_ota_begin failed");
        return false;
    }

    esp_http_client_config_t cfg = {};
    cfg.url        = m.url;
    cfg.timeout_ms = OTA_HTTP_TIMEOUT_MS;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { esp_ota_abort(handle); return false; }

    bool ok = (esp_http_client_open(client, 0) == ESP_OK);
    if (ok) esp_http_client_fetch_headers(client);

    Sha256Streamer hasher;
    uint8_t buf[1024];
    size_t total = 0;
    while (ok) {
        int n = esp_http_client_read(client, reinterpret_cast<char*>(buf), sizeof(buf));
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        if (esp_ota_write(handle, buf, n) != ESP_OK) { ok = false; break; }
        hasher.update(buf, n);
        total += n;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!ok) {
        Serial.printf("[OTA] download failed at %u bytes\n", (unsigned)total);
        esp_ota_abort(handle);
        return false;
    }
    if (total != m.size) {
        Serial.printf("[OTA] size mismatch got=%u want=%u\n",
                      (unsigned)total, (unsigned)m.size);
        esp_ota_abort(handle);
        return false;
    }

    char hex[65] = {};
    hasher.finalizeToHex(hex);
    if (!hasher.matches(m.sha256)) {
        Serial.printf("[OTA] sha256 mismatch got=%s want=%s\n", hex, m.sha256);
        esp_ota_abort(handle);
        return false;
    }

    if (esp_ota_end(handle) != ESP_OK) {
        Serial.println("[OTA] esp_ota_end failed");
        return false;
    }
    Serial.printf("[OTA] download bytes=%u sha256_ok=true\n", (unsigned)total);
    return true;
}

}  // namespace

namespace Ota {

void validateOnBoot() {
    char attempted[32] = {};
    OtaState::getAttempted(attempted, sizeof(attempted));

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    bool isPending = false;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
        isPending = (state == ESP_OTA_IMG_PENDING_VERIFY);
    }

    ValidateAction action = validateOnBootAction(FIRMWARE_VERSION, attempted, isPending);
    Serial.printf("[OTA] validate version=%s attempted=%s pending=%d action=%d\n",
                  FIRMWARE_VERSION, attempted, (int)isPending, (int)action);

    switch (action) {
        case ValidateAction::NoOp:
            break;
        case ValidateAction::ClearAttempted:
            OtaState::clearAttempted();
            OtaState::clearFailed();
            break;
        case ValidateAction::RecordFailed:
            OtaState::setFailed(attempted);
            OtaState::clearAttempted();
            break;
    }
}

void onPublishSuccess() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK) return;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return;

    Serial.println("[OTA] first publish ok — marking firmware valid");
    esp_ota_mark_app_valid_cancel_rollback();
    OtaState::clearAttempted();
    OtaState::clearFailed();
}

void checkAndApply(uint8_t batteryPct) {
    char host[64];
    readOtaHost(host, sizeof(host));

    char manifestUrl[160];
    snprintf(manifestUrl, sizeof(manifestUrl),
             "http://%s/firmware/sensor-tag-wifi/%s/manifest.json",
             host, OTA_VARIANT);

    char body[1024];
    int len = 0;
    if (!fetchManifestText(manifestUrl, body, sizeof(body), len)) {
        Serial.println("[OTA] manifest fetch failed");
        return;
    }

    Manifest m {};
    if (!parseManifest(body, (size_t)len, m)) {
        Serial.println("[OTA] manifest parse failed");
        return;
    }

    char failed[32] = {};
    OtaState::getFailed(failed, sizeof(failed));

    Serial.printf("[OTA] check version=%s manifest=%s failed=%s battery=%u\n",
                  FIRMWARE_VERSION, m.version, failed, batteryPct);

    if (!shouldApply(FIRMWARE_VERSION, m.version, failed, batteryPct)) {
        Serial.println("[OTA] check → skip");
        return;
    }

    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target) { Serial.println("[OTA] no inactive partition"); return; }

    if (!downloadAndStream(m, target)) return;

    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        Serial.println("[OTA] set_boot_partition failed");
        return;
    }
    OtaState::setAttempted(m.version);
    Serial.printf("[OTA] reboot into %s\n", m.version);
    Serial.flush();
    esp_restart();
}

}  // namespace Ota
```

- [ ] **Step 3: Verify the device build links**

```bash
cd firmware/sensor-tag-wifi
pio run -e xiao-c6-sht31 2>&1 | tail -10
```

Expected: build succeeds. Code is unreachable (nothing calls `Ota::` yet) so no behavioural change.

- [ ] **Step 4: Commit**

```bash
git add firmware/sensor-tag-wifi/include/ota.h \
        firmware/sensor-tag-wifi/src/ota.cpp
git commit -m "feat(sensor-tag-wifi): HTTP-pull OTA shell (validate/publish-hook/apply)"
```

---

## Task 9: Wire OTA into main.cpp

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/main.cpp`

- [ ] **Step 1: Add the include**

Edit `firmware/sensor-tag-wifi/src/main.cpp`. Insert after the `#include "serial_console.h"` line:

```cpp
#include "ota.h"
```

- [ ] **Step 2: Track last battery reading and add publish hook**

Inside the anonymous namespace, after the `RTC_DATA_ATTR uint16_t rtcSampleCounter = 0;` line, add:

```cpp
uint8_t lastBatteryPct = 0;
```

In `sampleAndEnqueue()`, after `r.battery_pct = Battery::readPercent();`, add:

```cpp
    lastBatteryPct = r.battery_pct;
```

In `drainBuffer()`, change the publish loop from:
```cpp
        if (!MqttClient::publish(deviceId, r)) break;
        RingBuffer::popOldest();
        sent++;
```
to:
```cpp
        if (!MqttClient::publish(deviceId, r)) break;
        RingBuffer::popOldest();
        sent++;
        Ota::onPublishSuccess();
```

- [ ] **Step 3: Add validateOnBoot + checkAndApply to setup()**

In `setup()`, immediately after `initDeviceId();` and the device-ID print:

```cpp
    Ota::validateOnBoot();
```

In `setup()`, immediately before the `Serial.printf("[MAIN] sleeping ...` line:

```cpp
    if (rtcSampleCounter == 0 && lastBatteryPct > 0) {
        Ota::checkAndApply(lastBatteryPct);
    }
```

(Guard ensures we only check OTA on cycles where we just uploaded — `rtcSampleCounter == 0` after a successful drain.)

- [ ] **Step 4: Verify the build succeeds**

```bash
cd firmware/sensor-tag-wifi
pio run -e xiao-c6-sht31 2>&1 | tail -10
pio run -e xiao-c6-ds18b20 2>&1 | tail -10
```

Expected: both build successfully.

- [ ] **Step 5: Run native tests as a regression check**

```bash
cd firmware/sensor-tag-wifi
pio test -e native
```

Expected: all tests still pass (6 payload + 5 manifest + 6 decision + 4 validation + 5 sha256 = 26 tests).

- [ ] **Step 6: Commit**

```bash
git add firmware/sensor-tag-wifi/src/main.cpp
git commit -m "feat(sensor-tag-wifi): wire OTA validate/hook/check into main flow"
```

---

## Task 10: Serial console accepts `set ota_host`

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/serial_console.cpp`

- [ ] **Step 1: Add ota_host to known keys**

Edit `firmware/sensor-tag-wifi/src/serial_console.cpp`. Replace the `knownKeys[]` initializer with:

```cpp
const char* knownKeys[] = {
    "hive_id", "collector_mac", "day_start", "day_end", "read_interval",
    "weight_off", "weight_scl", "mqtt_host", "mqtt_port", "mqtt_user", "mqtt_pass",
    "tag_name", "tag_name_2", "adv_interval",
    "wifi_ssid", "wifi_pass",
    "sample_int", "upload_every",
    "ota_host"
};
```

The existing string-detection branch in `printKeyValue()` already handles `ota_host` (it's a string with content; the early `prefs.getString` path catches it). The `set` handler already routes string values containing dots/letters to `putString`, so no further change is needed.

- [ ] **Step 2: Verify the build**

```bash
cd firmware/sensor-tag-wifi
pio run -e xiao-c6-sht31 2>&1 | tail -5
```

Expected: build succeeds.

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/src/serial_console.cpp
git commit -m "feat(sensor-tag-wifi): expose ota_host to serial provisioning console"
```

---

## Task 11: provision_tag.py gains --ota-host

**Files:**
- Modify: `tools/provision_tag.py`

- [ ] **Step 1: Refactor to accept CLI args and add --ota-host**

Replace the entire contents of `tools/provision_tag.py` with:

```python
#!/usr/bin/env python3
"""Provision a sensor-tag-wifi via its serial console.

Waits for the C6's USB CDC to appear, sends provisioning commands over the
console, then exits. Override defaults via CLI flags.
"""
import argparse
import glob
import sys
import time
import serial


PORT_GLOB = "/dev/cu.usbmodem*"
BAUD = 115200
SHELL_PROMPT = b"> "


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--wifi-ssid", default="IOT")
    p.add_argument("--wifi-pass", default="4696930759")
    p.add_argument("--mqtt-host", default="192.168.1.82")
    p.add_argument("--mqtt-port", default="1883")
    p.add_argument("--mqtt-user", default="hivesense")
    p.add_argument("--mqtt-pass", default="hivesense")
    p.add_argument("--tag-name",  default="bench-ds18b20")
    p.add_argument("--sample-int", default="30")
    p.add_argument("--upload-every", default="1")
    p.add_argument("--ota-host",  default="192.168.1.61")
    return p.parse_args()


def build_commands(a: argparse.Namespace) -> list[str]:
    return [
        f"set wifi_ssid {a.wifi_ssid}",
        f"set wifi_pass {a.wifi_pass}",
        f"set mqtt_host {a.mqtt_host}",
        f"set mqtt_port {a.mqtt_port}",
        f"set mqtt_user {a.mqtt_user}",
        f"set mqtt_pass {a.mqtt_pass}",
        f"set tag_name {a.tag_name}",
        f"set sample_int {a.sample_int}",
        f"set upload_every {a.upload_every}",
        f"set ota_host {a.ota_host}",
        "list",
        "exit",
    ]


def find_port(timeout_s: float = 60.0) -> str:
    print(f"[prov] waiting up to {timeout_s:.0f}s for {PORT_GLOB} ...")
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        matches = glob.glob(PORT_GLOB)
        if matches:
            port = matches[0]
            print(f"[prov] found {port}")
            return port
        time.sleep(0.1)
    print("[prov] port never appeared — reconnect the C6 USB cable")
    sys.exit(1)


def open_serial(port: str, timeout_s: float = 5.0) -> serial.Serial:
    deadline = time.time() + timeout_s
    last_err = None
    while time.time() < deadline:
        try:
            return serial.Serial(port, BAUD, timeout=0.05)
        except (serial.SerialException, OSError) as err:
            last_err = err
            time.sleep(0.05)
    raise RuntimeError(f"could not open {port}: {last_err}")


def _read(ser: serial.Serial, n: int) -> bytes:
    try:
        return ser.read(n)
    except serial.SerialException:
        time.sleep(0.05)
        return b""


def wait_for(ser: serial.Serial, needle: bytes, timeout_s: float) -> bool:
    buf = bytearray()
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        chunk = _read(ser, 256)
        if chunk:
            buf.extend(chunk)
            sys.stdout.write(chunk.decode(errors="replace"))
            sys.stdout.flush()
            if needle in buf:
                return True
    return False


def drain(ser: serial.Serial, duration_s: float) -> None:
    end = time.time() + duration_s
    while time.time() < end:
        chunk = _read(ser, 256)
        if chunk:
            sys.stdout.write(chunk.decode(errors="replace"))
            sys.stdout.flush()


def main() -> None:
    args = parse_args()
    commands = build_commands(args)

    port = find_port()
    with open_serial(port) as ser:
        print("[prov] opened — probing for console")
        ser.write(b"help\r")
        if not wait_for(ser, SHELL_PROMPT, timeout_s=5.0):
            print("\n[prov] no shell prompt — reconnect the C6 and retry")
            return

        for cmd in commands:
            print(f"\n[prov] >> {cmd}")
            ser.write(cmd.encode() + b"\r")
            wait_for(ser, SHELL_PROMPT, timeout_s=3.0)

        print("\n[prov] provisioned — tailing serial for 60s")
        drain(ser, 60.0)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Sanity-check the script**

```bash
python3 tools/provision_tag.py --help
```

Expected: argparse `--help` output listing all flags including `--ota-host`. No serial port required.

- [ ] **Step 3: Commit**

```bash
git add tools/provision_tag.py
git commit -m "feat(provision_tag): expose --ota-host plus other provisioning flags"
```

---

## Task 12: nginx fragment for /firmware/

**Files:**
- Modify: `deploy/web/nginx/combsense-web.conf`

- [ ] **Step 1: Add the firmware location to the :80 server**

Edit `deploy/web/nginx/combsense-web.conf`. Replace the `:80` server block with:

```nginx
# --- HTTP (:80) — ACME challenge passthrough; firmware on LAN; rest → HTTPS
server {
    listen 80;
    listen [::]:80;
    server_name dashboard.combsense.com 192.168.1.61;

    location /.well-known/acme-challenge/ {
        root /var/www/html;
    }

    # LAN-only firmware OTA pull. Integrity is sha256 in the manifest;
    # the allow/deny block prevents accidental WAN exposure.
    location /firmware/ {
        alias /var/www/combsense-firmware/;
        autoindex off;
        default_type application/octet-stream;
        types { application/json json; application/octet-stream bin; }

        allow 192.168.0.0/16;
        deny all;
    }

    location / {
        return 301 https://$host$request_uri;
    }
}
```

The `server_name` now also accepts `192.168.1.61` so devices using the IP form bypass the HTTPS redirect for `/firmware/` requests.

- [ ] **Step 2: Deploy to the LXC**

```bash
ssh natas@192.168.1.61 "sudo mkdir -p /var/www/combsense-firmware/sensor-tag-wifi/sht31 /var/www/combsense-firmware/sensor-tag-wifi/ds18b20"

scp deploy/web/nginx/combsense-web.conf natas@192.168.1.61:/tmp/combsense-web.conf
ssh natas@192.168.1.61 "sudo mv /tmp/combsense-web.conf /etc/nginx/sites-available/combsense-web && sudo nginx -t && sudo systemctl reload nginx"
```

Expected: `nginx -t` reports `syntax is ok` and `test is successful`. Reload returns 0.

- [ ] **Step 3: Sanity-check the route**

```bash
# From a workstation on 192.168.x.x (not the LXC itself):
curl -i http://192.168.1.61/firmware/
```

Expected: `HTTP/1.1 403 Forbidden` (autoindex off, no index file). 403 is the success signal — confirms the location matches and is reachable; no redirect to HTTPS.

If it returns `301 Moved Permanently` to `https://...`, the `location /firmware/` block isn't matching — verify it's *before* the catch-all `location /`.

- [ ] **Step 4: Commit**

```bash
git add deploy/web/nginx/combsense-web.conf
git commit -m "deploy(web): nginx fragment for LAN-only /firmware/ OTA pulls"
```

---

## Task 13: publish-firmware.sh

**Files:**
- Create: `deploy/web/publish-firmware.sh`

- [ ] **Step 1: Write the publish script**

`deploy/web/publish-firmware.sh`:
```bash
#!/usr/bin/env bash
# Build sensor-tag-wifi firmware for <variant>, upload to the OTA host, and
# publish a fresh manifest. Run from the repo root.
#
# Usage:  deploy/web/publish-firmware.sh <sht31|ds18b20>

set -euo pipefail

VARIANT="${1:-}"
if [[ "$VARIANT" != "sht31" && "$VARIANT" != "ds18b20" ]]; then
    echo "usage: $0 <sht31|ds18b20>" >&2
    exit 2
fi

OTA_HOST="${OTA_HOST:-192.168.1.61}"
OTA_USER="${OTA_USER:-natas}"
OTA_ROOT="/var/www/combsense-firmware/sensor-tag-wifi/${VARIANT}"
WEB_BASE="http://${OTA_HOST}/firmware/sensor-tag-wifi/${VARIANT}"

cd firmware/sensor-tag-wifi

VERSION="$(git describe --tags --always)"
echo "[publish] variant=${VARIANT} version=${VERSION}"

pio run -e "xiao-c6-${VARIANT}"
BIN=".pio/build/xiao-c6-${VARIANT}/firmware.bin"
[[ -f "$BIN" ]] || { echo "build did not produce ${BIN}" >&2; exit 1; }

SHA="$(shasum -a 256 "$BIN" | awk '{print $1}')"
SIZE="$(stat -f '%z' "$BIN" 2>/dev/null || stat -c '%s' "$BIN")"
echo "[publish] sha256=${SHA} size=${SIZE}"

MANIFEST=$(mktemp)
cat > "$MANIFEST" <<EOF
{
  "version": "${VERSION}",
  "url": "${WEB_BASE}/${VERSION}/firmware.bin",
  "sha256": "${SHA}",
  "size": ${SIZE}
}
EOF

scp "$BIN" "${OTA_USER}@${OTA_HOST}:/tmp/firmware.bin"
scp "$MANIFEST" "${OTA_USER}@${OTA_HOST}:/tmp/manifest.json"
rm -f "$MANIFEST"

ssh "${OTA_USER}@${OTA_HOST}" sudo bash -s <<EOF
set -euo pipefail
mkdir -p "${OTA_ROOT}/${VERSION}"
mv /tmp/firmware.bin "${OTA_ROOT}/${VERSION}/firmware.bin"
chmod 644 "${OTA_ROOT}/${VERSION}/firmware.bin"
mv /tmp/manifest.json "${OTA_ROOT}/manifest.json"
chmod 644 "${OTA_ROOT}/manifest.json"
EOF

echo "[publish] done — manifest at ${WEB_BASE}/manifest.json"
```

- [ ] **Step 2: Make it executable**

```bash
chmod +x deploy/web/publish-firmware.sh
```

- [ ] **Step 3: Sanity-check via dry argv handling**

```bash
deploy/web/publish-firmware.sh
```

Expected: `usage: deploy/web/publish-firmware.sh <sht31|ds18b20>` on stderr, exit code 2.

```bash
deploy/web/publish-firmware.sh bogus
```

Expected: same usage error, exit code 2.

- [ ] **Step 4: Commit**

```bash
git add deploy/web/publish-firmware.sh
git commit -m "deploy(web): publish-firmware.sh — build, upload, manifest in one shot"
```

---

## Task 14: Bootstrap publish + first OTA

**Files:** none (operational task).

- [ ] **Step 1: Publish current firmware as the seed manifest**

For both variants, the running firmware on a device must match the manifest version on first run, otherwise it'll try to update on first wake (which is fine, but adds noise to the test).

Easiest path: publish first, then flash the same firmware over USB so the device boots with `FIRMWARE_VERSION` matching the manifest.

```bash
# From the repo root
deploy/web/publish-firmware.sh ds18b20
```

Expected: script exits 0; `curl http://192.168.1.61/firmware/sensor-tag-wifi/ds18b20/manifest.json` from the LAN returns the JSON manifest with the version produced by `git describe`.

```bash
curl http://192.168.1.61/firmware/sensor-tag-wifi/ds18b20/manifest.json
curl -I http://192.168.1.61/firmware/sensor-tag-wifi/ds18b20/$(git describe --tags --always)/firmware.bin
```

Second curl should return `HTTP/1.1 200 OK` and `Content-Type: application/octet-stream`.

- [ ] **Step 2: Flash the device with the same version**

```bash
cd firmware/sensor-tag-wifi
pio run -e xiao-c6-ds18b20 -t upload
pio device monitor -e xiao-c6-ds18b20
```

Expected serial output: `[OTA] validate version=<git-describe> attempted= pending=0 action=0` (NoOp); after first publish: `[OTA] check version=<v> manifest=<v> failed= battery=<n>` followed by `[OTA] check → skip`.

- [ ] **Step 3: Commit a no-op marker (optional)**

If the publish workflow surfaces any tweaks to the script during real use, capture them here. Otherwise skip the commit.

---

## Task 15: Hardware smoke tests

**Files:** none — operational; record results in the runbook.

Each scenario from the spec's "Hardware smoke tests" matrix. Tail serial during each.

- [ ] **Step 1: Happy-path upgrade**

Make a trivial change (e.g. bump a log string in `main.cpp`), commit, push to a tag, then:

```bash
git tag -a v0.2.0-ota-test -m "OTA smoke test"
deploy/web/publish-firmware.sh ds18b20
```

On the device: tail serial across 1–2 wake cycles. Expected log lines:
```
[OTA] check version=<old> manifest=v0.2.0-ota-test failed= battery=<n>
[OTA] download bytes=<n> sha256_ok=true
[OTA] reboot into v0.2.0-ota-test
... (reboot)
[OTA] validate version=v0.2.0-ota-test attempted=v0.2.0-ota-test pending=1 action=0
... (after first publish)
[OTA] first publish ok — marking firmware valid
```

Verify a fresh sample arrives in InfluxDB:
```bash
ssh root@192.168.1.19 "influx query --token \$(jq -r .ios_read_token /root/.combsense-tsdb-creds 2>/dev/null || cat /root/.combsense-tsdb-creds | grep ios_read_token | cut -d'=' -f2) --org combsense 'from(bucket:\"combsense\") |> range(start:-10m) |> filter(fn: (r) => r.sensor_id == \"<your-id>\") |> last()'"
```

- [ ] **Step 2: Same-version no-op**

Re-run `deploy/web/publish-firmware.sh ds18b20` without changing code. Expected serial: `[OTA] check ... → skip`. nginx access log shows `GET /firmware/.../manifest.json` but no GET on `firmware.bin`.

- [ ] **Step 3: SHA-256 mismatch**

```bash
ssh natas@192.168.1.61 "sudo bash -c 'V=\$(jq -r .version /var/www/combsense-firmware/sensor-tag-wifi/ds18b20/manifest.json); printf \"\\xff\" | dd of=/var/www/combsense-firmware/sensor-tag-wifi/ds18b20/\$V/firmware.bin bs=1 count=1 conv=notrunc'"
```

Bump version in manifest by hand or republish to invalidate the cached check. Tail serial. Expected: `[OTA] sha256 mismatch got=<x> want=<y>` then sleep, no boot-partition change.

Restore by re-running `publish-firmware.sh`.

- [ ] **Step 4: Failed-publish rollback**

In a temporary branch, intentionally break MQTT (e.g. set the wrong `mqtt_host` default in code or add a `return false` in `MqttClient::publish`). Tag and publish.

Expected: device boots new firmware → tries to publish → fails → on next reset, bootloader rolls back → old firmware sees `attempted != FIRMWARE_VERSION` and records `failed=<broken-version>`. Subsequent wakes log `[OTA] check ... → skip` because manifest matches `failed`.

Recover by reverting the broken commit, retagging, and republishing — the new tag differs from `failed`, so the device will attempt the upgrade.

- [ ] **Step 5: Battery floor**

Build with `-DOTA_BATTERY_FLOOR_PCT=99` (override via PIO env or temporarily edit `config.h`), flash, publish a new version. Expected: `[OTA] check ... battery=<n> → skip`. Revert the override after the test.

- [ ] **Step 6: Capture results**

Append observed serial lines for each scenario to a runbook section (e.g. add to `.mex/ROUTER.md` "TSDB / firmware ops" or create `docs/superpowers/runbooks/sensor-tag-wifi-ota.md` if it doesn't exist).

```bash
git add docs/superpowers/runbooks/sensor-tag-wifi-ota.md  # if created
git commit -m "docs(sensor-tag-wifi): OTA hardware smoke test results"
```

If no doc was created, no commit needed.

---

## Task 16: Update .mex/ROUTER.md

**Files:**
- Modify: `.mex/ROUTER.md`

- [ ] **Step 1: Add OTA bullet to "Completed" under sensor-tag-wifi**

Edit `.mex/ROUTER.md`. In the `Sensor tag WiFi variant` block, append a bullet:

```
  - HTTP-pull OTA on wake (manifest at http://192.168.1.61/firmware/sensor-tag-wifi/<variant>/manifest.json), sha256-verified, dual 1.5 MB OTA slots, bootloader auto-rollback if first publish fails. Publish via `deploy/web/publish-firmware.sh <variant>`.
```

- [ ] **Step 2: Update Routing Table**

Add a row:

| Sensor-tag-wifi OTA | `firmware/sensor-tag-wifi/src/ota*.cpp` + `deploy/web/publish-firmware.sh` |

- [ ] **Step 3: Update last_updated and Phase line**

Bump frontmatter `last_updated` to today's date with a one-line summary (e.g. `2026-04-24 (sensor-tag-wifi OTA shipped — HTTP-pull from combsense-web LXC)`).

- [ ] **Step 4: Commit**

```bash
git add .mex/ROUTER.md
git commit -m "docs(mex): note sensor-tag-wifi OTA pipeline in ROUTER"
```

---

## Self-Review

Spec coverage check (against `docs/superpowers/specs/2026-04-24-sensor-tag-wifi-ota-design.md`):

| Spec requirement | Task |
|---|---|
| `Ota::validateOnBoot/onPublishSuccess/checkAndApply` API | Task 8 (impl), Task 9 (wiring) |
| `ota_manifest` pure parser | Task 1 |
| `ota_decision` pure functions | Task 2 |
| `ota_state` Preferences adapter | Task 4 |
| `ota_sha256` streaming verifier | Task 3 |
| `include/config.h` extensions | Task 7 |
| Dual 1.5 MB partition table | Task 6 |
| `extra_scripts = pre:get_version.py` + `OTA_VARIANT` | Task 5 |
| `serial_console` accepts `set ota_host` | Task 10 |
| `provision_tag.py --ota-host` | Task 11 |
| nginx fragment + LXC bootstrap | Task 12 |
| `publish-firmware.sh` | Task 13 |
| Native unit tests (manifest/decision/validation/sha256) | Tasks 1-3 |
| Hardware smoke tests | Task 15 |
| Runbook hooks | Tasks 14-16 |

All 26 native tests cover every row of the `shouldApply` and `validateOnBootAction` truth tables in the spec.

**Type consistency check:** `Manifest`, `ValidateAction`, `Sha256Streamer`, `OtaState::*` names, `Ota::*` names match across all tasks. The native env's `build_src_filter` accumulates correctly: `payload.cpp`, `ota_manifest.cpp`, `ota_decision.cpp`, `ota_sha256.cpp` (no transient missing-symbol gap between tasks).

**No placeholders:** every step contains the actual code, command, or file edit needed.
