#pragma once

#include <cstddef>
#include <cstdint>

// =============================================================================
// Pin Definitions
//
// Defaults target the Seeed XIAO ESP32-C6.
//   PIN_ONE_WIRE_GPIO    — C6 default: GPIO2 (D2, 4.7 kΩ pullup to 3V3)
//   PIN_BATTERY_ADC_GPIO — C6 default: GPIO0 (A0)
//
// Override at build time via -DPIN_ONE_WIRE_GPIO=N / -DPIN_BATTERY_ADC_GPIO=N
// (e.g. Waveshare ESP32-S3-Zero uses GPIO4 / GPIO1).
// =============================================================================

// I2C (SHT31) — D4/D5 on XIAO C6; not overridable (SHT31 S3 variant not built)
constexpr uint8_t PIN_I2C_SDA = 4;
constexpr uint8_t PIN_I2C_SCL = 5;

// 1-Wire (DS18B20)
#ifndef PIN_ONE_WIRE_GPIO
#define PIN_ONE_WIRE_GPIO 2
#endif
constexpr uint8_t PIN_ONE_WIRE = PIN_ONE_WIRE_GPIO;

// Battery ADC
#ifndef PIN_BATTERY_ADC_GPIO
#define PIN_BATTERY_ADC_GPIO 0
#endif
constexpr uint8_t PIN_BATTERY_ADC = PIN_BATTERY_ADC_GPIO;

// =============================================================================
// Power / Timing Defaults
// =============================================================================

constexpr uint16_t DEFAULT_SAMPLE_INTERVAL_SEC = 300;   // 5 min (U16 max = 18h)
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

constexpr size_t PAYLOAD_MAX_LEN = 160;

// =============================================================================
// OTA
// =============================================================================

constexpr const char* OTA_DEFAULT_HOST     = "192.168.1.61";
constexpr uint8_t     OTA_BATTERY_FLOOR_PCT = 20;
constexpr uint32_t    OTA_HTTP_TIMEOUT_MS  = 30000;
constexpr const char* NVS_KEY_OTA_HOST     = "ota_host";
