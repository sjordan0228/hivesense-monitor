#pragma once

#include <cstdint>

// =============================================================================
// Pin Definitions — LilyGO T-SIM7080G-S3
// =============================================================================

// SIM7080G modem
constexpr uint8_t PIN_MODEM_RXD    = 4;
constexpr uint8_t PIN_MODEM_TXD    = 5;
constexpr uint8_t PIN_MODEM_PWRKEY = 41;
constexpr uint8_t PIN_MODEM_RI     = 3;
constexpr uint8_t PIN_MODEM_DTR    = 42;

// SD card (not used in v1, reserved)
constexpr uint8_t PIN_SD_CLK  = 38;
constexpr uint8_t PIN_SD_CMD  = 39;
constexpr uint8_t PIN_SD_DATA = 40;

// PMU (power management unit)
constexpr uint8_t PIN_PMU_SDA = 15;
constexpr uint8_t PIN_PMU_SCL = 7;
constexpr uint8_t PIN_PMU_IRQ = 6;

// =============================================================================
// Modem Configuration
// =============================================================================

constexpr uint32_t MODEM_BAUD_RATE    = 115200;
constexpr uint16_t MODEM_PWRKEY_MS    = 1000;
constexpr uint16_t MODEM_BOOT_WAIT_MS = 5000;
constexpr uint16_t NETWORK_TIMEOUT_MS = 30000;

// =============================================================================
// Timing
// =============================================================================

constexpr uint8_t  PUBLISH_INTERVAL_MIN    = 30;
constexpr uint16_t MQTT_CONNECT_TIMEOUT_MS = 10000;

// =============================================================================
// Buffer
// =============================================================================

constexpr uint8_t MAX_HIVE_NODES = 20;

// =============================================================================
// NVS Keys
// =============================================================================

constexpr const char* NVS_NAMESPACE      = "hivesense";
constexpr const char* NVS_KEY_MQTT_HOST  = "mqtt_host";
constexpr const char* NVS_KEY_MQTT_PORT  = "mqtt_port";
constexpr const char* NVS_KEY_MQTT_USER  = "mqtt_user";
constexpr const char* NVS_KEY_MQTT_PASS  = "mqtt_pass";

// =============================================================================
// MQTT Topics
// =============================================================================

constexpr const char* MQTT_TOPIC_PREFIX = "hivesense/hive/";
constexpr const char* MQTT_OTA_TOPIC    = "hivesense/ota/start";

// =============================================================================
// OTA
// =============================================================================

constexpr const char* GITHUB_RELEASE_BASE = "https://github.com/sjordan0228/hivesense-monitor/releases/download/";
