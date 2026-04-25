#pragma once

#include <cstddef>

struct Manifest {
    char version[32];
    char url[256];
    char sha256[65];   // 64 hex chars + null
    size_t size;
};

bool parseManifest(const char* json, size_t len, Manifest& out);
