#pragma once

#include "config_parser.h"
#include <cstddef>
#include <cstdint>

/// Unified per-key result record for the config ack pipeline.
///
/// PR-1 populates result as "ok" (applied) or leaves NVS-error entries as
/// "invalid:nvs".  PR-2 will use the full `category:detail` vocabulary:
///   "ok", "unchanged", "unknown_key", "excluded:<reason>",
///   "invalid:<reason>", "conflict:<other_key>"
struct AckEntry {
    char key[16];
    char result[32];
};

/// Pre-validation hook: enforces cross-key constraints before the NVS apply
/// loop.  Must be called with the fully-parsed ConfigUpdate; may populate
/// outEntries with conflict/exclusion AckEntry values.
///
/// PR-1: no rules — always returns true, outCount set to 0.
/// PR-2: will enforce feat_ds18b20 ⊕ feat_sht31 mutual exclusion and any
///       additional constraint rules.
///
/// Returns true when the caller may proceed to applyConfigToNvs.
bool preValidate(const ConfigParser::ConfigUpdate& parsed,
                 AckEntry* outEntries, size_t* outCount);
