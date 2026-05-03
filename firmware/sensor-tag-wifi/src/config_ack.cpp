#include "config_ack.h"
#include "scale_math.h"

#include <ArduinoJson.h>
#include <cstring>

using ConfigParser::FeatFlag;

namespace {

/// Resolve the post-apply state of a feat flag given the parsed value and the
/// current NVS state.  "Absent" in parsed → keep current NVS value.
static bool resolveFlag(FeatFlag parsed, bool currentNvs) {
    if (parsed == FeatFlag::Absent) return currentNvs;
    return parsed == FeatFlag::On;
}

static void appendEntry(AckEntry* entries, size_t* count,
                        const char* key, const char* result) {
    if (count == nullptr) return;
    AckEntry& e = entries[*count];
    strncpy(e.key,    key,    sizeof(e.key)    - 1); e.key[sizeof(e.key) - 1] = '\0';
    strncpy(e.result, result, sizeof(e.result) - 1); e.result[sizeof(e.result) - 1] = '\0';
    (*count)++;
}

}  // namespace

bool isConfigGetExcluded(const char* key) {
    // §4.3/§7.2: these keys would brick the tag if mistyped remotely.
    // Never return them in config/state, even if explicitly requested.
    return strcmp(key, "wifi_pass") == 0 ||
           strcmp(key, "mqtt_pass") == 0;
}

bool anyFeatKeyPresent(const AckEntry* entries, size_t numEntries) {
    for (size_t i = 0; i < numEntries; ++i) {
        if (strncmp(entries[i].key, "feat_", 5) == 0) return true;
    }
    return false;
}

size_t buildRichAck(const AckEntry* entries, size_t numEntries,
                    int64_t nowEpoch,
                    char* out, size_t outCap) {
    JsonDocument doc;
    doc["event"] = "config_applied";

    JsonObject results = doc["results"].to<JsonObject>();
    for (size_t i = 0; i < numEntries; ++i) {
        results[entries[i].key] = entries[i].result;
    }

    char tsBuf[22] = {};
    if (nowEpoch <= 0 || formatRFC3339(nowEpoch, tsBuf, sizeof(tsBuf)) == 0) {
        strncpy(tsBuf, "1970-01-01T00:00:00Z", sizeof(tsBuf) - 1);
    }
    doc["ts"] = tsBuf;

    return serializeJson(doc, out, outCap);
}

bool preValidate(const ConfigParser::ConfigUpdate& parsed,
                 const TemperatureNvsState& nvsState,
                 AckEntry* outEntries, size_t* outCount) {
    if (outCount) *outCount = 0;

    // Compute post-apply state for the two mutually-exclusive temp sensors.
    bool ds18b20_after = resolveFlag(parsed.feat_ds18b20, nvsState.ds18b20_enabled);
    bool sht31_after   = resolveFlag(parsed.feat_sht31,   nvsState.sht31_enabled);

    if (ds18b20_after && sht31_after) {
        // Both would be enabled — report conflict on whichever key(s) the
        // payload touched.  If neither key was in the payload this branch is
        // unreachable (both would mirror the NVS state which should never be
        // simultaneously enabled), but guard defensively.
        if (outEntries && outCount) {
            // Report on feat_sht31: conflicts with feat_ds18b20
            appendEntry(outEntries, outCount, "feat_sht31",   "conflict:feat_ds18b20");
            // Report on feat_ds18b20: conflicts with feat_sht31
            appendEntry(outEntries, outCount, "feat_ds18b20", "conflict:feat_sht31");
        }
        return false;
    }

    return true;
}
