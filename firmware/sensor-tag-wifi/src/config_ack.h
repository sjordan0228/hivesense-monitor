#pragma once

#include "config_parser.h"
#include <cstddef>
#include <cstdint>

/// Unified per-key result record for the config ack pipeline.
///
/// Full `category:detail` vocabulary per config-mqtt-contract.md §6:
///   "ok", "unchanged", "unknown_key", "excluded:<reason>",
///   "invalid:<reason>", "conflict:<other_key>"
struct AckEntry {
    char key[16];
    char result[32];
};

/// Current NVS state of the two mutually-exclusive temperature sensor flags.
/// Passed to preValidate so the function is pure C++ (no NVS I/O) and
/// fully testable in the native env.
struct TemperatureNvsState {
    bool ds18b20_enabled;  ///< current NVS value (or compile-time default)
    bool sht31_enabled;    ///< current NVS value (or compile-time default)
};

/// Pre-validation hook: enforces cross-key constraints before the NVS apply
/// loop.  Must be called with the fully-parsed ConfigUpdate and the current
/// NVS state of the mutually-exclusive temp-sensor flags.
///
/// Conflict rule: if the post-apply state would have both feat_ds18b20 AND
/// feat_sht31 enabled, both keys are appended to outEntries with the
/// appropriate "conflict:<other_key>" result, and false is returned.
///
/// Returns true when the caller may proceed to applyConfigToNvs.
bool preValidate(const ConfigParser::ConfigUpdate& parsed,
                 const TemperatureNvsState& nvsState,
                 AckEntry* outEntries, size_t* outCount);

/// Returns true if any entry in the given ack array has a key that starts with
/// "feat_" — used to gate capabilities re-publish per contract §3.2.
///
/// "Touched" means any category (ok, unchanged, invalid, conflict) for a
/// feat_* key.  Pure non-feat config writes (sample_int, tag_name, etc.)
/// should not trigger a capabilities re-publish.
bool anyFeatKeyPresent(const AckEntry* entries, size_t numEntries);

/// Per-policy exclusion: returns true when the named key is excluded from
/// config/state responses (wifi_pass, mqtt_pass per §4.3/§7.2).
/// Used by handleConfigGet to prevent sensitive keys from leaking, even if
/// explicitly requested in the `keys` filter.
bool isConfigGetExcluded(const char* key);

/// Build the rich ack JSON payload per config-mqtt-contract.md §5.1:
///
///   {"event":"config_applied","results":{<key>:<result>,...},"ts":"..."}
///
/// entries[0..numEntries) supply the per-key results.
/// nowEpoch is the current Unix timestamp (0 → sentinel "1970-01-01T00:00:00Z").
/// Writes NUL-terminated JSON to out[0..outCap); returns bytes written (excl NUL)
/// or 0 on failure.
size_t buildRichAck(const AckEntry* entries, size_t numEntries,
                    int64_t nowEpoch,
                    char* out, size_t outCap);
