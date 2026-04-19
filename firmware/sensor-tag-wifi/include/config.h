#pragma once

#include <cstdint>

// =============================================================================
// Pin Definitions — Seeed XIAO ESP32-C6
// =============================================================================

// I2C (SHT31) — D4/D5
constexpr uint8_t PIN_I2C_SDA = 4;
constexpr uint8_t PIN_I2C_SCL = 5;

// 1-Wire (DS18B20) — D2 with 4.7kΩ pullup to 3V3
constexpr uint8_t PIN_ONE_WIRE = 2;

// Battery ADC — A0 (GPIO 0 on XIAO C6)
constexpr uint8_t PIN_BATTERY_ADC = 0;

// =============================================================================
// Power / Timing Defaults
// =============================================================================

constexpr uint32_t DEFAULT_SAMPLE_INTERVAL_SEC = 300;   // 5 min
constexpr uint8_t  DEFAULT_UPLOAD_EVERY_N      = 1;     // Upload every sample
constexpr uint16_t WIFI_CONNECT_TIMEOUT_MS     = 10000;
constexpr uint16_t MQTT_CONNECT_TIMEOUT_MS     = 5000;
constexpr uint8_t  WIFI_RECONNECT_RETRIES      = 2;

// =============================================================================
// MQTT Defaults
// =============================================================================

constexpr const char* DEFAULT_MQTT_HOST  = "192.168.1.82";
constexpr uint16_t    DEFAULT_MQTT_PORT  = 1883;
constexpr const char* MQTT_TOPIC_PREFIX  = "combsense/hive/";

// =============================================================================
// NVS
// =============================================================================

constexpr const char* NVS_NAMESPACE        = "combsense";
constexpr const char* NVS_KEY_WIFI_SSID    = "wifi_ssid";
constexpr const char* NVS_KEY_WIFI_PASS    = "wifi_pass";
constexpr const char* NVS_KEY_MQTT_HOST    = "mqtt_host";
constexpr const char* NVS_KEY_MQTT_PORT    = "mqtt_port";
constexpr const char* NVS_KEY_MQTT_USER    = "mqtt_user";
constexpr const char* NVS_KEY_MQTT_PASS    = "mqtt_pass";
constexpr const char* NVS_KEY_TAG_NAME     = "tag_name";
constexpr const char* NVS_KEY_SAMPLE_INT   = "sample_int";
constexpr const char* NVS_KEY_UPLOAD_EVERY = "upload_every";

// =============================================================================
// RTC Ring Buffer
// =============================================================================

constexpr uint8_t RTC_BUFFER_CAPACITY = 48;   // 4h @ 5-min cadence

// =============================================================================
// Sensors
// =============================================================================

constexpr uint8_t SHT31_ADDR                    = 0x44;
constexpr uint16_t DS18B20_CONVERT_TIMEOUT_MS   = 800;
constexpr uint8_t  DS18B20_RESOLUTION_BITS      = 12;

// =============================================================================
// Payload
// =============================================================================

constexpr uint8_t PAYLOAD_VERSION = 1;
constexpr size_t  PAYLOAD_MAX_LEN = 160;
