#include "serial_console.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>
#include <cstdlib>

namespace {

constexpr const char* NVS_NS = "combsense";
constexpr uint16_t CONSOLE_WAIT_MS = 3000;

const char* knownKeys[] = {
    "hive_id", "collector_mac", "day_start", "day_end", "read_interval",
    "weight_off", "weight_scl", "mqtt_host", "mqtt_port", "mqtt_user", "mqtt_pass",
    "tag_name", "tag_name_2", "adv_interval",
    "wifi_ssid", "wifi_pass",
    "sample_int", "upload_every"
};
constexpr uint8_t NUM_KNOWN_KEYS = sizeof(knownKeys) / sizeof(knownKeys[0]);

/// Read a full line from Serial, blocking until newline or buffer full.
bool readLine(char* buffer, uint16_t maxLen) {
    uint16_t pos = 0;
    while (pos < maxLen - 1) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (pos > 0) break;  // ignore leading newlines
                continue;
            }
            buffer[pos++] = c;
            Serial.print(c);  // echo
        }
        delay(1);
    }
    buffer[pos] = '\0';
    Serial.println();
    return pos > 0;
}

/// Print a single NVS key's value, detecting type automatically.
void printKeyValue(Preferences& prefs, const char* key) {
    // Check MAC (6-byte blob) first
    if (strcmp(key, "collector_mac") == 0) {
        size_t len = prefs.getBytesLength(key);
        if (len == 6) {
            uint8_t mac[6];
            prefs.getBytes(key, mac, 6);
            Serial.printf("  %-15s = %02X:%02X:%02X:%02X:%02X:%02X\n", key,
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return;
        }
    }

    // Try string
    String strVal = prefs.getString(key, "");
    if (strVal.length() > 0) {
        Serial.printf("  %-15s = %s\n", key, strVal.c_str());
        return;
    }

    // Try float (check if key exists first to distinguish 0.0 from not-set)
    if (prefs.isKey(key)) {
        // Could be numeric — try float first (covers weight_off, weight_scl)
        if (strcmp(key, "weight_off") == 0 || strcmp(key, "weight_scl") == 0) {
            Serial.printf("  %-15s = %.4f\n", key, prefs.getFloat(key, 0.0f));
            return;
        }
        // Try ushort (mqtt_port, sample_int)
        if (strcmp(key, "mqtt_port") == 0 || strcmp(key, "sample_int") == 0) {
            Serial.printf("  %-15s = %u\n", key, prefs.getUShort(key, 0));
            return;
        }
        // Default to uchar (day_start, day_end, read_interval)
        Serial.printf("  %-15s = %u\n", key, prefs.getUChar(key, 0));
        return;
    }

    Serial.printf("  %-15s = (not set)\n", key);
}

/// Parse and execute a console command.
bool handleCommand(Preferences& prefs, const char* line) {
    char cmd[16] = {};
    char key[32] = {};
    char value[128] = {};

    // Parse up to 3 tokens — value may contain spaces for strings
    int parsed = sscanf(line, "%15s %31s", cmd, key);

    // For set/set_mac, extract value as everything after the second space
    if (parsed >= 2 && (strcmp(cmd, "set") == 0 || strcmp(cmd, "set_mac") == 0)) {
        const char* valueStart = line;
        int spaces = 0;
        while (*valueStart && spaces < 2) {
            if (*valueStart == ' ') spaces++;
            valueStart++;
        }
        if (*valueStart) {
            strncpy(value, valueStart, sizeof(value) - 1);
        }
    }

    if (strcmp(cmd, "help") == 0) {
        Serial.println("Commands:");
        Serial.println("  get <key>                        Read NVS value");
        Serial.println("  set <key> <value>                Write NVS value");
        Serial.println("  set_mac <key> AA:BB:CC:DD:EE:FF  Write MAC address");
        Serial.println("  list                             Show all keys");
        Serial.println("  reboot                           Restart device");
        Serial.println("  exit                             Resume normal boot");
        return true;
    }

    if (strcmp(cmd, "list") == 0) {
        Serial.println("NVS contents:");
        for (uint8_t i = 0; i < NUM_KNOWN_KEYS; i++) {
            printKeyValue(prefs, knownKeys[i]);
        }
        return true;
    }

    if (strcmp(cmd, "get") == 0) {
        if (strlen(key) == 0) {
            Serial.println("Usage: get <key>");
            return true;
        }
        printKeyValue(prefs, key);
        return true;
    }

    if (strcmp(cmd, "set") == 0) {
        if (strlen(key) == 0 || strlen(value) == 0) {
            Serial.println("Usage: set <key> <value>");
            return true;
        }

        // Type detection — a float has exactly one dot and only digits otherwise
        int dotCount = 0;
        bool isFloat = false;
        for (const char* p = value; *p; p++) {
            if (*p == '.') dotCount++;
            else if (*p < '0' || *p > '9') { dotCount = 99; break; }
        }
        isFloat = (dotCount == 1);

        if (isFloat) {
            float f = atof(value);
            prefs.putFloat(key, f);
            Serial.printf("  %s = %.4f (float)\n", key, f);
        } else {
            // Check if purely numeric
            bool isNumeric = true;
            for (const char* p = value; *p; p++) {
                if (*p < '0' || *p > '9') { isNumeric = false; break; }
            }

            if (isNumeric) {
                long num = atol(value);
                if (num <= 255) {
                    prefs.putUChar(key, static_cast<uint8_t>(num));
                    Serial.printf("  %s = %u (uint8)\n", key, static_cast<uint8_t>(num));
                } else if (num <= 65535) {
                    prefs.putUShort(key, static_cast<uint16_t>(num));
                    Serial.printf("  %s = %u (uint16)\n", key, static_cast<uint16_t>(num));
                } else {
                    prefs.putString(key, value);
                    Serial.printf("  %s = %s (string)\n", key, value);
                }
            } else {
                prefs.putString(key, value);
                Serial.printf("  %s = %s (string)\n", key, value);
            }
        }
        return true;
    }

    if (strcmp(cmd, "set_mac") == 0) {
        if (strlen(key) == 0 || strlen(value) == 0) {
            Serial.println("Usage: set_mac <key> AA:BB:CC:DD:EE:FF");
            return true;
        }

        uint8_t mac[6];
        char valueCopy[32];
        strncpy(valueCopy, value, sizeof(valueCopy) - 1);

        uint8_t idx = 0;
        char* token = strtok(valueCopy, ":");
        while (token != nullptr && idx < 6) {
            mac[idx++] = static_cast<uint8_t>(strtol(token, nullptr, 16));
            token = strtok(nullptr, ":");
        }

        if (idx == 6) {
            prefs.putBytes(key, mac, 6);
            Serial.printf("  %s = %02X:%02X:%02X:%02X:%02X:%02X\n", key,
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            Serial.println("Invalid MAC format. Use AA:BB:CC:DD:EE:FF");
        }
        return true;
    }

    if (strcmp(cmd, "reboot") == 0) {
        Serial.println("Rebooting...");
        Serial.flush();
        prefs.end();
        ESP.restart();
        return false;  // unreachable
    }

    if (strcmp(cmd, "exit") == 0) {
        return false;  // exit console
    }

    Serial.printf("Unknown command: %s (type 'help')\n", cmd);
    return true;
}

/// Interactive console loop.
void runConsole() {
    Preferences prefs;
    prefs.begin(NVS_NS, false);  // read-write

    Serial.println("\n=== CombSense Serial Console ===");
    Serial.println("Type 'help' for commands, 'exit' to resume boot.\n");

    char line[128];
    while (true) {
        Serial.print("> ");
        if (!readLine(line, sizeof(line))) continue;
        if (!handleCommand(prefs, line)) break;
    }

    prefs.end();
    Serial.println("Resuming normal operation...\n");
}

}  // anonymous namespace

namespace SerialConsole {

void checkForConsole() {
    Serial.println("[CONSOLE] Press any key within 3s to enter provisioning console...");

    uint32_t start = millis();
    while (millis() - start < CONSOLE_WAIT_MS) {
        if (Serial.available()) {
            while (Serial.available()) Serial.read();  // flush input
            runConsole();
            return;
        }
        delay(10);
    }
}

}  // namespace SerialConsole
