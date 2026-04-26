#include "ota_state.h"

#include <Preferences.h>
#include <cstring>

namespace {
constexpr const char* OTA_NS      = "ota";
constexpr const char* K_ATTEMPTED = "attempted";
constexpr const char* K_FAILED    = "failed";

void readKey(const char* key, char* out, size_t outCap) {
    Preferences p;
    p.begin(OTA_NS, true);
    String v = p.getString(key, "");
    p.end();
    size_t n = v.length() < outCap ? v.length() : outCap - 1;
    memcpy(out, v.c_str(), n);
    out[n] = '\0';
}

void writeKey(const char* key, const char* value) {
    Preferences p;
    p.begin(OTA_NS, false);
    p.putString(key, value);
    p.end();
}

void removeKey(const char* key) {
    Preferences p;
    p.begin(OTA_NS, false);
    p.remove(key);
    p.end();
}
}  // namespace

namespace OtaState {

void getAttempted(char* out, size_t outCap) { readKey(K_ATTEMPTED, out, outCap); }
void setAttempted(const char* v)            { writeKey(K_ATTEMPTED, v); }
void clearAttempted()                       { removeKey(K_ATTEMPTED); }

void getFailed(char* out, size_t outCap)    { readKey(K_FAILED, out, outCap); }
void setFailed(const char* v)               { writeKey(K_FAILED, v); }
void clearFailed()                          { removeKey(K_FAILED); }

}  // namespace OtaState
