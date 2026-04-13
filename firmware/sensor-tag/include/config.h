#pragma once

#include <cstdint>

// =============================================================================
// Pin Definitions — Seeed XIAO ESP32C6
// =============================================================================

constexpr uint8_t PIN_I2C_SDA = 4;   // D4
constexpr uint8_t PIN_I2C_SCL = 5;   // D5

// =============================================================================
// BLE Advertisement
// =============================================================================

constexpr uint16_t MANUFACTURER_ID         = 0xFFFF;  // Prototyping
constexpr uint8_t  TAG_PROTOCOL_VERSION    = 0x01;
constexpr uint16_t DEFAULT_ADV_INTERVAL_SEC = 60;
constexpr uint16_t ADV_DURATION_MS         = 200;

// =============================================================================
// NVS
// =============================================================================

constexpr const char* NVS_NAMESPACE    = "hivesense";
constexpr const char* NVS_KEY_TAG_NAME = "tag_name";
constexpr const char* NVS_KEY_ADV_INT  = "adv_interval";

// =============================================================================
// SHT31
// =============================================================================

constexpr uint8_t SHT31_ADDR = 0x44;
