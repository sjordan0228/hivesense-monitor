#pragma once

#include <cstdint>
#include <cstddef>

/// Pure parser for incoming MQTT config messages. Compiled and unit-tested
/// host-side (native env). No Arduino, ArduinoJson-only via implementation.
///
/// See issue #25 for the full spec. Allowed keys (v1):
///
///   sample_int    uint16  range 30..3600
///   upload_every  uint8   range 1..60
///   tag_name      string  ≤63 chars (NUL-terminated, fits in 64 buf)
///   ota_host      string  ≤63 chars (NUL-terminated, fits in 64 buf)
///   feat_ds18b20  uint8   0 or 1 only; invalid:not_0_or_1 otherwise
///   feat_sht31    uint8   0 or 1 only; mutually exclusive with feat_ds18b20
///   feat_scale    uint8   0 or 1 only
///   feat_mic      uint8   0 or 1 only
///
/// All other keys are silently rejected — including v1-excluded keys
/// (wifi_ssid, wifi_pass, mqtt_*) which would brick the device if
/// misconfigured remotely.

namespace ConfigParser {

constexpr uint16_t SAMPLE_INT_MIN     = 30;
constexpr uint16_t SAMPLE_INT_MAX     = 3600;
constexpr uint8_t  UPLOAD_EVERY_MIN   = 1;
constexpr uint8_t  UPLOAD_EVERY_MAX   = 60;
constexpr size_t   TAG_NAME_MAX_LEN   = 64;
constexpr size_t   OTA_HOST_MAX_LEN   = 64;
constexpr size_t   MAX_REJECTED_KEYS  = 8;
constexpr size_t   REJECTED_KEY_LEN   = 32;

/// Parsed feat_* flag value — distinguishes "not present" from 0/1.
enum class FeatFlag : uint8_t {
    Absent = 0xFF,  ///< key not in the payload
    Off    = 0,
    On     = 1,
};

/// Parse outcome. has_<x> flags indicate which keys were successfully
/// extracted from the payload and pass validation. Rejected keys are
/// listed by name for the ack message.
struct ConfigUpdate {
    bool     has_sample_int;
    uint16_t sample_int;

    bool     has_upload_every;
    uint8_t  upload_every;

    bool     has_tag_name;
    char     tag_name[TAG_NAME_MAX_LEN];

    bool     has_ota_host;
    char     ota_host[OTA_HOST_MAX_LEN];

    // Feature flags — FeatFlag::Absent when the key was not in the payload.
    FeatFlag feat_ds18b20;
    FeatFlag feat_sht31;
    FeatFlag feat_scale;
    FeatFlag feat_mic;

    uint8_t  num_rejected;
    char     rejected[MAX_REJECTED_KEYS][REJECTED_KEY_LEN];
};

/// Parse and validate a config JSON payload.
///
/// Returns true when JSON parses successfully (regardless of whether any
/// keys passed validation — partial apply is allowed; check has_<x>
/// flags). Returns false only when the input is not valid JSON, or is
/// not a JSON object at the top level.
///
/// Side effects: zeroes `out` before populating.
bool parse(const char* json, ConfigUpdate& out);

}  // namespace ConfigParser
