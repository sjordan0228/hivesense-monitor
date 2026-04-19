#include <Arduino.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_sleep.h>

#include "config.h"
#include "reading.h"
#include "sensor.h"
#include "battery.h"
#include "payload.h"
#include "ring_buffer.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "serial_console.h"

namespace {

RTC_DATA_ATTR uint16_t rtcSampleCounter = 0;

char deviceId[9] = {0};

/// Derive an 8-hex-char device ID from the eFuse MAC (low 4 bytes).
void initDeviceId() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(deviceId, sizeof(deviceId), "%02x%02x%02x%02x",
             mac[2], mac[3], mac[4], mac[5]);
}

struct Config {
    uint32_t sampleIntervalSec;
    uint8_t  uploadEveryN;
};

Config loadConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    Config c {
        .sampleIntervalSec = prefs.getUInt(NVS_KEY_SAMPLE_INT, DEFAULT_SAMPLE_INTERVAL_SEC),
        .uploadEveryN      = prefs.getUChar(NVS_KEY_UPLOAD_EVERY, DEFAULT_UPLOAD_EVERY_N),
    };
    prefs.end();
    return c;
}

/// Drain the RTC ring buffer over MQTT. Leaves unsent readings in place.
void drainBuffer() {
    if (RingBuffer::size() == 0) return;

    if (!WifiManager::connect()) {
        Serial.println("[MAIN] no wifi — keeping buffer");
        return;
    }

    if (!MqttClient::connect(deviceId)) {
        Serial.println("[MAIN] no mqtt — keeping buffer");
        WifiManager::disconnect();
        return;
    }

    uint8_t sent = 0;
    while (RingBuffer::size() > 0) {
        Reading r;
        if (!RingBuffer::peekOldest(r)) break;
        if (!MqttClient::publish(deviceId, r)) break;
        RingBuffer::popOldest();
        sent++;
    }
    Serial.printf("[MAIN] sent %u / remaining %u\n", sent, RingBuffer::size());

    MqttClient::disconnect();
    WifiManager::disconnect();
}

/// Take one sensor sample and push it into the ring buffer.
void sampleAndEnqueue() {
    if (!Sensor::begin()) {
        Serial.println("[MAIN] sensor init failed — skipping sample");
        Sensor::deinit();
        return;
    }

    Reading r {};
    bool ok = Sensor::read(r);
    Sensor::deinit();
    if (!ok) {
        Serial.println("[MAIN] sensor read failed");
        return;
    }

    r.battery_pct = Battery::readPercent();
    r.timestamp   = 0;   // Filled by NTP during drainBuffer if we have no time yet

    // If we haven't set system time yet, tag with zero — the collector/backend
    // can backfill. If time is already set (previous wake), use it.
    time_t now;
    time(&now);
    if (now > 1700000000) r.timestamp = static_cast<uint32_t>(now);

    RingBuffer::push(r);
    Serial.printf("[MAIN] sample t1=%.2f t2=%.2f h1=%.2f h2=%.2f b=%u buffered=%u\n",
                  r.temp1, r.temp2, r.humidity1, r.humidity2, r.battery_pct,
                  RingBuffer::size());
}

}  // anonymous namespace

void setup() {
    Serial.begin(115200);
    delay(500);

    initDeviceId();
    Serial.printf("[MAIN] combsense sensor-tag-wifi id=%s\n", deviceId);

    RingBuffer::initIfColdBoot();

    // Ensure NVS namespace exists for first boot
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.end();

    SerialConsole::checkForConsole();

    Config cfg = loadConfig();
    Serial.printf("[MAIN] sample_int=%lus upload_every=%u\n",
                  (unsigned long)cfg.sampleIntervalSec, cfg.uploadEveryN);

    sampleAndEnqueue();
    rtcSampleCounter++;

    if (rtcSampleCounter >= cfg.uploadEveryN) {
        // If we don't yet have a valid timestamp on the oldest reading, do a
        // quick NTP sync via the WiFi connect we'd do anyway.
        drainBuffer();
        rtcSampleCounter = 0;
    } else {
        Serial.printf("[MAIN] not uploading this cycle (%u/%u)\n",
                      rtcSampleCounter, cfg.uploadEveryN);
    }

    Serial.printf("[MAIN] sleeping %lus\n", (unsigned long)cfg.sampleIntervalSec);
    Serial.flush();
    esp_deep_sleep(static_cast<uint64_t>(cfg.sampleIntervalSec) * 1000000ULL);
}

void loop() {
    // Never reached — deep sleep restarts from setup()
}
