#ifdef SENSOR_SCALE

#include "scale.h"
#include "scale_math.h"
#include "scale_commands.h"
#include "config.h"
#include "mqtt_client.h"
#include "payload.h"

#include <Arduino.h>
#include <HX711.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <time.h>

namespace {

HX711 hx711;
Preferences prefs;

int64_t weight_off_   = 0;
double  weight_scl_   = HX711_DEFAULT_SCALE_FACTOR;
StableDetector stable_;

bool extended_awake_ = false;
int64_t keep_alive_until_ = 0;
uint32_t last_heartbeat_ms_ = 0;

bool streaming_ = false;
int64_t stream_until_ = 0;
uint32_t last_stream_ms_ = 0;

bool modify_active_ = false;
char modify_label_[32] = {};
double modify_pre_kg_ = 0.0;
int64_t modify_started_at_ = 0;
int64_t modify_timeout_at_ = 0;

constexpr const char* NVS_NS = "combsense";
constexpr const char* NVS_K_OFF = "weight_off";
constexpr const char* NVS_K_SCL = "weight_scl";

int64_t nowEpoch() {
    time_t t = time(nullptr);
    return static_cast<int64_t>(t);
}

void loadFromNvs() {
    prefs.begin(NVS_NS, /*readOnly=*/true);
    weight_off_ = prefs.getLong64(NVS_K_OFF, 0);
    weight_scl_ = prefs.getDouble(NVS_K_SCL, HX711_DEFAULT_SCALE_FACTOR);
    prefs.end();
}

void writeOffsetToNvs(int64_t off) {
    prefs.begin(NVS_NS, /*readOnly=*/false);
    prefs.putLong64(NVS_K_OFF, off);
    prefs.end();
    weight_off_ = off;
}

void writeScaleToNvs(double scl) {
    prefs.begin(NVS_NS, /*readOnly=*/false);
    prefs.putDouble(NVS_K_SCL, scl);
    prefs.end();
    weight_scl_ = scl;
}

}  // anonymous namespace

namespace {

char status_topic_[80] = {};
char cmd_topic_[80]    = {};
char config_topic_[80] = {};

void buildTopics(const char* deviceId) {
    snprintf(status_topic_, sizeof(status_topic_),
             "combsense/hive/%s/scale/status", deviceId);
    snprintf(cmd_topic_, sizeof(cmd_topic_),
             "combsense/hive/%s/scale/cmd", deviceId);
    snprintf(config_topic_, sizeof(config_topic_),
             "combsense/hive/%s/scale/config", deviceId);
}

void publishStatusEvent(const char* json) {
    if (status_topic_[0] == '\0') return;
    MqttClient::publishRaw(status_topic_, json, /*retained=*/false);
}

// Forward declarations for helpers defined later in this namespace
void enterExtendedAwake(int64_t kau);
void exitExtendedAwake();

void handleConfigMessage(const char* payload, unsigned int len) {
    if (len == 0) {
        if (extended_awake_) exitExtendedAwake();
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, payload, len)) return;
    const char* kau_str = doc["keep_alive_until"].as<const char*>();
    if (!kau_str) return;

    // Parse RFC3339 (UTC only) → epoch using sscanf (strptime not available on ESP32 newlib)
    int y, mo, d, h, mi, s;
    if (sscanf(kau_str, "%d-%d-%dT%d:%d:%dZ", &y, &mo, &d, &h, &mi, &s) != 6) return;
    struct tm tm_;
    memset(&tm_, 0, sizeof(tm_));
    tm_.tm_year = y - 1900;
    tm_.tm_mon  = mo - 1;
    tm_.tm_mday = d;
    tm_.tm_hour = h;
    tm_.tm_min  = mi;
    tm_.tm_sec  = s;
    // timegm not available on ESP32 newlib; temporarily set TZ to UTC and use mktime
    setenv("TZ", "UTC0", 1);
    tzset();
    int64_t kau = static_cast<int64_t>(mktime(&tm_));

    int64_t now = nowEpoch();
    if (Scale::ntpSynced()) {
        if (!isKeepAliveValid(kau, now)) return;
    } else {
        kau = now + KEEPALIVE_NTP_FALLBACK_SEC;
    }
    enterExtendedAwake(kau);
}

bool readSamples(int32_t* out, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) {
        if (!hx711.wait_ready_timeout(HX711_READ_TIMEOUT_MS)) return false;
        out[i] = hx711.read();
        stable_.push(out[i]);
    }
    return true;
}

void cmdTare() {
    int32_t samples[HX711_TARE_SAMPLE_COUNT];
    if (!readSamples(samples, HX711_TARE_SAMPLE_COUNT)) {
        char buf[160];
        serializeErrorEvent("hx711_unresponsive", "tare failed: no DOUT pulse",
                            nowEpoch(), buf, sizeof(buf));
        publishStatusEvent(buf);
        return;
    }
    int64_t off = tareFromMean(samples, HX711_TARE_SAMPLE_COUNT);
    writeOffsetToNvs(off);

    char buf[160];
    serializeTareSavedEvent(off, nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
}

void cmdCalibrate(double known_kg) {
    int32_t samples[HX711_TARE_SAMPLE_COUNT];
    if (!readSamples(samples, HX711_TARE_SAMPLE_COUNT)) {
        char buf[160];
        serializeErrorEvent("hx711_unresponsive", "calibrate failed: no DOUT pulse",
                            nowEpoch(), buf, sizeof(buf));
        publishStatusEvent(buf);
        return;
    }
    double sf = scaleFactorFromMean(samples, HX711_TARE_SAMPLE_COUNT, weight_off_, known_kg);
    if (std::fabs(sf) < HX711_CALIBRATE_MIN_FACTOR) {
        char buf[200];
        char detail[64];
        snprintf(detail, sizeof(detail), "scale_factor=%.4f below threshold", sf);
        serializeErrorEvent("calibrate_invalid", detail, nowEpoch(), buf, sizeof(buf));
        publishStatusEvent(buf);
        return;
    }
    writeScaleToNvs(sf);

    // Stub predicted accuracy = sample stddev / mean * 100
    int64_t sum = 0;
    for (uint8_t i = 0; i < HX711_TARE_SAMPLE_COUNT; i++) sum += samples[i];
    double mean = double(sum) / HX711_TARE_SAMPLE_COUNT;
    double var = 0;
    for (uint8_t i = 0; i < HX711_TARE_SAMPLE_COUNT; i++) {
        double d = samples[i] - mean;
        var += d * d;
    }
    double stddev = std::sqrt(var / HX711_TARE_SAMPLE_COUNT);
    double predicted = (mean != 0.0) ? (stddev / std::fabs(mean) * 100.0) : 0.0;

    char buf[200];
    serializeCalibrationSavedEvent(sf, predicted, nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
}

void cmdVerify(double expected_kg) {
    int32_t samples[HX711_VERIFY_SAMPLE_COUNT];
    if (!readSamples(samples, HX711_VERIFY_SAMPLE_COUNT)) {
        char buf[160];
        serializeErrorEvent("hx711_unresponsive", "verify failed: no DOUT pulse",
                            nowEpoch(), buf, sizeof(buf));
        publishStatusEvent(buf);
        return;
    }
    int64_t sum = 0;
    for (uint8_t i = 0; i < HX711_VERIFY_SAMPLE_COUNT; i++) sum += samples[i];
    int32_t avg = sum / HX711_VERIFY_SAMPLE_COUNT;
    double measured = applyCalibration(avg, weight_off_, weight_scl_);
    double err = errorPct(measured, expected_kg);

    char buf[200];
    serializeVerifyResultEvent(measured, expected_kg, err, nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
}

void cmdStreamRaw(int32_t duration_sec) {
    streaming_ = true;
    int64_t cap = std::min<int32_t>(duration_sec, 120);  // hard cap per spec
    stream_until_ = nowEpoch() + cap;
    last_stream_ms_ = millis();
}

void cmdStopStream() {
    streaming_ = false;
}

void cmdModifyStart(const char* label) {
    int32_t raw;
    double kg;
    Scale::sample(raw, kg);  // best-effort; OK if HX711 is flaky here
    modify_pre_kg_ = std::isfinite(kg) ? kg : 0.0;
    std::strncpy(modify_label_, label, sizeof(modify_label_) - 1);
    modify_label_[sizeof(modify_label_) - 1] = '\0';
    modify_started_at_ = nowEpoch();
    modify_timeout_at_ = modify_started_at_ + MODIFY_DEFAULT_TIMEOUT_SEC;
    modify_active_ = true;

    char buf[200];
    serializeModifyStartedEvent(modify_label_, modify_pre_kg_, nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
}

void cmdModifyEnd(const char* label) {
    if (!modify_active_) return;
    if (strcmp(modify_label_, label) != 0) {
        char buf[200];
        serializeErrorEvent("modify_label_mismatch", label, nowEpoch(), buf, sizeof(buf));
        publishStatusEvent(buf);
        return;
    }
    int32_t raw;
    double kg;
    Scale::sample(raw, kg);
    double post_kg = std::isfinite(kg) ? kg : 0.0;
    double delta = post_kg - modify_pre_kg_;
    int32_t duration = static_cast<int32_t>(nowEpoch() - modify_started_at_);

    char buf[256];
    if (std::fabs(delta) < MODIFY_DELTA_THRESHOLD_KG) {
        serializeModifyWarningEvent(modify_label_, delta, "no_significant_change_detected",
                                    nowEpoch(), buf, sizeof(buf));
    } else {
        serializeModifyCompleteEvent(modify_label_, modify_pre_kg_, post_kg, delta,
                                     duration, false, nowEpoch(), buf, sizeof(buf));
    }
    publishStatusEvent(buf);
    modify_active_ = false;
}

void cmdModifyCancel() {
    modify_active_ = false;
}

void publishHeartbeat() {
    char buf[160];
    serializeAwakeEvent(keep_alive_until_, nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
    last_heartbeat_ms_ = millis();
}

void enterExtendedAwake(int64_t kau) {
    extended_awake_    = true;
    keep_alive_until_  = kau;
    last_heartbeat_ms_ = 0;  // force immediate heartbeat
    publishHeartbeat();
}

void exitExtendedAwake() {
    extended_awake_   = false;
    keep_alive_until_ = 0;
    streaming_        = false;
    modify_active_    = false;
}

void publishStreamSample() {
    int32_t raw;
    double kg;
    if (!Scale::sample(raw, kg)) return;
    char buf[200];
    serializeRawStreamEvent(raw, kg, stable_.isStable(), nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
}

}  // anonymous namespace

namespace Scale {

void init() {
    hx711.begin(PIN_HX711_DT_, PIN_HX711_SCK_, HX711_GAIN);
    hx711.power_up();
    loadFromNvs();
    stable_.reset();
    extended_awake_ = false;
    keep_alive_until_ = 0;
    streaming_ = false;
    modify_active_ = false;
}

void deinit() {
    hx711.power_down();
}

bool sample(int32_t& raw, double& kg) {
    if (!hx711.wait_ready_timeout(HX711_READ_TIMEOUT_MS)) {
        kg = NAN;
        raw = 0;
        return false;
    }
    raw = hx711.read();
    stable_.push(raw);
    kg = applyCalibration(raw, weight_off_, weight_scl_);
    return true;
}

void subscribe() {
    if (cmd_topic_[0] == '\0') buildTopics(MqttClient::getDeviceId());
    MqttClient::subscribe(cmd_topic_);
    MqttClient::subscribe(config_topic_);
}

void onMessage(const char* topic, const char* payload, unsigned int len) {
    if (strcmp(topic, config_topic_) == 0) {
        handleConfigMessage(payload, len);
        return;
    }
    if (strcmp(topic, cmd_topic_) == 0) {
        ScaleCommand cmd;
        if (!parseScaleCommand(payload, cmd)) return;
        switch (cmd.type) {
            case ScaleCommandType::Tare:         cmdTare(); break;
            case ScaleCommandType::Calibrate:    cmdCalibrate(cmd.calibrate.known_kg); break;
            case ScaleCommandType::Verify:       cmdVerify(cmd.verify.expected_kg); break;
            case ScaleCommandType::StreamRaw:    cmdStreamRaw(cmd.stream_raw.duration_sec); break;
            case ScaleCommandType::StopStream:   cmdStopStream(); break;
            case ScaleCommandType::ModifyStart:  cmdModifyStart(cmd.modify.label); break;
            case ScaleCommandType::ModifyEnd:    cmdModifyEnd(cmd.modify.label); break;
            case ScaleCommandType::ModifyCancel: cmdModifyCancel(); break;
        }
    }
}

void onConnect() {
    subscribe();
    uint32_t deadline = millis() + RETAINED_CONFIG_WAIT_MS;
    while (millis() < deadline) {
        MqttClient::loop(10);
        if (extended_awake_) break;
    }
}

void tick() {
    if (!extended_awake_) return;
    int64_t now = nowEpoch();

    // Exit if keep-alive expired (modulo skew)
    if (!isKeepAliveValid(keep_alive_until_, now)) {
        exitExtendedAwake();
        return;
    }

    // Heartbeat every 60s
    if ((millis() - last_heartbeat_ms_) >= HEARTBEAT_INTERVAL_MS) {
        publishHeartbeat();
    }

    // Stream sampling at 1Hz
    if (streaming_) {
        if (now >= stream_until_) {
            streaming_ = false;
        } else if ((millis() - last_stream_ms_) >= HX711_STREAM_INTERVAL_MS) {
            publishStreamSample();
            last_stream_ms_ = millis();
        }
    }

    // Modify timeout
    if (modify_active_ && now >= modify_timeout_at_) {
        char buf[160];
        serializeModifyTimeoutEvent(modify_label_, now, buf, sizeof(buf));
        publishStatusEvent(buf);
        modify_active_ = false;
    }
}

bool ntpSynced() {
    // Heuristic: if epoch is well past 2020, NTP has fired.
    return nowEpoch() > 1577836800;  // 2020-01-01 UTC
}

bool inExtendedAwakeMode() {
    return extended_awake_;
}

int64_t keepAliveUntil() {
    return keep_alive_until_;
}

}  // namespace Scale

#else  // !SENSOR_SCALE — provide no-op stubs so main.cpp compiles unchanged

#include "scale.h"
#include <cstdint>
#include <cmath>

namespace Scale {
void init() {}
void deinit() {}
bool sample(int32_t&, double& kg) { kg = NAN; return false; }
void subscribe() {}
void onMessage(const char*, const char*, unsigned int) {}
void tick() {}
bool inExtendedAwakeMode() { return false; }
int64_t keepAliveUntil() { return 0; }
void onConnect() {}
bool ntpSynced() { return true; }
}

#endif  // SENSOR_SCALE
