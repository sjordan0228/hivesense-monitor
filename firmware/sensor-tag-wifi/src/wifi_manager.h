#pragma once

#include <cstdint>

namespace WifiManager {

/// Connect to the configured SSID. Uses BSSID+channel cached in RTC memory
/// for a faster reconnect; falls back to full scan on mismatch. Returns true
/// on success, false on timeout.
bool connect();

/// Disconnect and power off the radio.
void disconnect();

/// Get current unix time from NTP (requires prior connect()).
/// Returns 0 on failure.
uint32_t getUnixTime();

}  // namespace WifiManager
