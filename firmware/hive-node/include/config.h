#pragma once

#include <cstdint>

// =============================================================================
// Pin Definitions — ESP32-WROOM-32
// =============================================================================

// I2C bus (SHT31 x2)
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;

// HX711 weight ADC
constexpr uint8_t PIN_HX711_DOUT = 16;
constexpr uint8_t PIN_HX711_CLK  = 17;
constexpr uint8_t PIN_MOSFET_HX711 = 18;

// Battery ADC (ADC1_CH6, input-only)
constexpr uint8_t PIN_BATTERY_ADC = 34;

// Status LED
constexpr uint8_t PIN_STATUS_LED = 2;

// Phase 2 — IR array (reserved, do not use in Phase 1)
constexpr uint8_t PIN_MUX_S0     = 25;
constexpr uint8_t PIN_MUX_S1     = 26;
constexpr uint8_t PIN_MUX_S2     = 27;
constexpr uint8_t PIN_MUX_S3     = 14;
constexpr uint8_t PIN_MUX_EN_TX  = 32;
constexpr uint8_t PIN_MUX_EN_RX  = 33;
constexpr uint8_t PIN_MUX_SIG_TX = 4;
constexpr uint8_t PIN_MUX_SIG_RX = 35;
constexpr uint8_t PIN_MOSFET_IR  = 19;

// =============================================================================
// I2C Addresses
// =============================================================================

constexpr uint8_t SHT31_ADDR_INTERNAL = 0x44;
constexpr uint8_t SHT31_ADDR_EXTERNAL = 0x45;

// =============================================================================
// Timing Constants
// =============================================================================

constexpr uint8_t  DEFAULT_DAY_START_HOUR    = 6;   // 6 AM
constexpr uint8_t  DEFAULT_DAY_END_HOUR      = 20;  // 8 PM
constexpr uint8_t  DEFAULT_READ_INTERVAL_MIN = 30;
constexpr uint16_t MOSFET_STABILIZE_MS       = 100;
constexpr uint16_t BLE_ADVERTISE_TIMEOUT_MS  = 5000;
constexpr uint8_t  ESPNOW_MAX_RETRIES        = 3;
constexpr uint16_t ESPNOW_RETRY_DELAY_MS     = 2000;

// =============================================================================
// Storage
// =============================================================================

constexpr uint16_t MAX_STORED_READINGS = 500;

// =============================================================================
// BLE UUIDs — derived from "HiveSense" ASCII
// =============================================================================

constexpr const char* BLE_SERVICE_UUID       = "4E6F7200-7468-6976-6553-656E73650000";
constexpr const char* BLE_CHAR_SENSOR_LOG    = "4E6F7200-7468-6976-6553-656E73650001";
constexpr const char* BLE_CHAR_READING_COUNT = "4E6F7200-7468-6976-6553-656E73650002";
constexpr const char* BLE_CHAR_HIVE_ID       = "4E6F7200-7468-6976-6553-656E73650003";
constexpr const char* BLE_CHAR_CLEAR_LOG     = "4E6F7200-7468-6976-6553-656E73650004";

// =============================================================================
// NVS Keys
// =============================================================================

constexpr const char* NVS_NAMESPACE      = "hivesense";
constexpr const char* NVS_KEY_HIVE_ID    = "hive_id";
constexpr const char* NVS_KEY_COLLECTOR  = "collector_mac";
constexpr const char* NVS_KEY_DAY_START  = "day_start";
constexpr const char* NVS_KEY_DAY_END    = "day_end";
constexpr const char* NVS_KEY_INTERVAL   = "read_interval";
constexpr const char* NVS_KEY_WEIGHT_OFF = "weight_off";
constexpr const char* NVS_KEY_WEIGHT_SCL = "weight_scl";

// =============================================================================
// Payload Version
// =============================================================================

constexpr uint8_t PAYLOAD_VERSION = 1;
