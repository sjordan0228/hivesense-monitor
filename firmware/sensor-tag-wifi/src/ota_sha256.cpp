#include "ota_sha256.h"

#include <cstring>
#include <cctype>

#ifdef ESP_PLATFORM
#include "mbedtls/sha256.h"
struct Backend {
    mbedtls_sha256_context ctx;
    Backend() { mbedtls_sha256_init(&ctx); mbedtls_sha256_starts(&ctx, 0); }
    ~Backend() { mbedtls_sha256_free(&ctx); }
    void reset() {
        mbedtls_sha256_free(&ctx);
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
    }
    void update(const uint8_t* d, size_t n) { mbedtls_sha256_update(&ctx, d, n); }
    void finalize(uint8_t out[32]) { mbedtls_sha256_finish(&ctx, out); }
};
#else
#include "picosha2.h"
struct Backend {
    picosha2::hash256_one_by_one hasher;
    Backend() { hasher.init(); }
    void reset() { hasher.init(); }
    void update(const uint8_t* d, size_t n) { hasher.process(d, d + n); }
    void finalize(uint8_t out[32]) {
        hasher.finish();
        hasher.get_hash_bytes(out, out + 32);
    }
};
#endif

namespace {
void toHex(const uint8_t in[32], char out[65]) {
    static const char* lut = "0123456789abcdef";
    for (size_t i = 0; i < 32; i++) {
        out[2 * i]     = lut[in[i] >> 4];
        out[2 * i + 1] = lut[in[i] & 0x0f];
    }
    out[64] = '\0';
}

bool hexEqIgnoreCase(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}
}  // namespace

Sha256Streamer::Sha256Streamer() : impl_(new Backend()) { lastHex_[0] = '\0'; }
Sha256Streamer::~Sha256Streamer() { delete static_cast<Backend*>(impl_); }

void Sha256Streamer::reset() { static_cast<Backend*>(impl_)->reset(); lastHex_[0] = '\0'; }
void Sha256Streamer::update(const uint8_t* data, size_t len) {
    static_cast<Backend*>(impl_)->update(data, len);
}
void Sha256Streamer::finalizeToHex(char outHex[65]) {
    uint8_t raw[32];
    Backend* backend = static_cast<Backend*>(impl_);
    backend->finalize(raw);
    toHex(raw, outHex);
    memcpy(lastHex_, outHex, 65);
    backend->reset();
}
bool Sha256Streamer::matches(const char* expectedHex) {
    if (lastHex_[0] == '\0') return false;
    return hexEqIgnoreCase(lastHex_, expectedHex);
}
