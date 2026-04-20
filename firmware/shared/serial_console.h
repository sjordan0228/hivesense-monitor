#pragma once

/// Interactive serial console for NVS provisioning.
/// Call checkForConsole() from setup() — if user presses a key within
/// 3 seconds, enters interactive mode. Otherwise returns immediately.
namespace SerialConsole {

    /// Check for serial input. Enters console mode if key detected.
    void checkForConsole();

    /// Enter console mode unconditionally and block until 'exit' or 'reboot'.
    void runBlocking();

}  // namespace SerialConsole
