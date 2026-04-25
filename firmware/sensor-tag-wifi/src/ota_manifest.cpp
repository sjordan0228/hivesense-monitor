#include "ota_manifest.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace {

const char* findKey(const char* json, size_t len, const char* key) {
    char needle[40];
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || (size_t)n >= sizeof(needle)) return nullptr;
    const char* end = json + len;
    for (const char* p = json; p + n <= end; p++) {
        if (memcmp(p, needle, n) != 0) continue;
        const char* after = p + n;
        while (after < end && isspace((unsigned char)*after)) after++;
        if (after < end && *after == ':') return p + n;
    }
    return nullptr;
}

bool extractString(const char* json, size_t len, const char* key,
                   char* out, size_t outCap) {
    const char* p = findKey(json, len, key);
    if (!p) return false;
    const char* end = json + len;
    while (p < end && *p != ':') p++;
    if (p == end) return false;
    p++;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p == end || *p != '"') return false;
    p++;
    const char* start = p;
    while (p < end && *p != '"') p++;
    if (p == end) return false;
    size_t length = (size_t)(p - start);
    if (length + 1 > outCap) return false;
    memcpy(out, start, length);
    out[length] = '\0';
    return true;
}

bool extractNumber(const char* json, size_t len, const char* key, size_t& out) {
    const char* p = findKey(json, len, key);
    if (!p) return false;
    const char* end = json + len;
    while (p < end && *p != ':') p++;
    if (p == end) return false;
    p++;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p == end || !isdigit((unsigned char)*p)) return false;
    char buf[24] = {};
    size_t i = 0;
    while (p < end && isdigit((unsigned char)*p) && i < sizeof(buf) - 1) {
        buf[i++] = *p++;
    }
    out = (size_t)strtoul(buf, nullptr, 10);
    return true;
}

}  // namespace

bool parseManifest(const char* json, size_t len, Manifest& out) {
    if (!json || len == 0) return false;
    if (!extractString(json, len, "version", out.version, sizeof(out.version))) return false;
    if (!extractString(json, len, "url",     out.url,     sizeof(out.url)))     return false;
    if (!extractString(json, len, "sha256",  out.sha256,  sizeof(out.sha256)))  return false;
    if (strlen(out.sha256) != 64) return false;
    if (!extractNumber(json, len, "size", out.size)) return false;
    return true;
}
