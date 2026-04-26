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
