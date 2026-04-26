#pragma once

#include <cstddef>

namespace OtaState {
    void getAttempted(char* out, size_t outCap);
    void setAttempted(const char* version);
    void clearAttempted();

    void getFailed(char* out, size_t outCap);
    void setFailed(const char* version);
    void clearFailed();
}
