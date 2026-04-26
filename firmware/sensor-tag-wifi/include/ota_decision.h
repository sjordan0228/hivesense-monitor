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
