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
        Serial.printf("[OTA] manifest fetch failed url=%s\n", manifestUrl);
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
