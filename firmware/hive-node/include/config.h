#pragma once

#include <cstdint>

// =============================================================================
// Pin Definitions — Freenove ESP32-S3 Lite
// Grouped by subsystem, ordered to minimize wire crossing.
// GPIO 26-32 reserved for flash/PSRAM. GPIO 0,3,45,46 are strapping pins.
// =============================================================================

// I2C pins freed — SHT31 removed, temp/humidity from wireless sensor tags
// GPIO 8, 9 available for future use

// HX711 weight ADC — adjacent pins for clean 2-wire run
constexpr uint8_t PIN_HX711_DOUT   = 10;
constexpr uint8_t PIN_HX711_CLK    = 11;
constexpr uint8_t PIN_MOSFET_HX711 = 12;

// Battery ADC (ADC1_CH0)
constexpr uint8_t PIN_BATTERY_ADC = 1;

// Onboard RGB LED — disabled at boot to save power
constexpr uint8_t PIN_ONBOARD_RGB = 48;

// Phase 2 — IR array (reserved, do not use in Phase 1)
constexpr uint8_t PIN_MUX_S0     = 4;   // Address lines sequential
constexpr uint8_t PIN_MUX_S1     = 5;
constexpr uint8_t PIN_MUX_S2     = 6;
constexpr uint8_t PIN_MUX_S3     = 7;
constexpr uint8_t PIN_MUX_EN_TX  = 13;  // Enable lines adjacent
constexpr uint8_t PIN_MUX_EN_RX  = 14;
constexpr uint8_t PIN_MUX_SIG_TX = 15;  // Signal lines together
constexpr uint8_t PIN_MUX_SIG_RX = 2;   // ADC1_CH1, internal pull-up for digital read
constexpr uint8_t PIN_MOSFET_IR  = 16;

// =============================================================================
// Timing Constants
// =============================================================================

constexpr uint8_t  DEFAULT_DAY_START_HOUR    = 6;
constexpr uint8_t  DEFAULT_DAY_END_HOUR      = 20;
constexpr uint8_t  DEFAULT_READ_INTERVAL_MIN = 30;
constexpr uint16_t MOSFET_STABILIZE_MS       = 100;
constexpr uint16_t BLE_ADVERTISE_TIMEOUT_MS  = 5000;
constexpr uint8_t  ESPNOW_MAX_RETRIES        = 3;
constexpr uint16_t ESPNOW_RETRY_DELAY_MS     = 2000;

// OTA
constexpr uint8_t  OTA_VALIDATION_TIMEOUT_SEC = 60;
constexpr uint16_t OTA_NVS_SAVE_INTERVAL      = 50;

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
