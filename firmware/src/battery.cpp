#include "battery.h"
#include "config.h"

#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

namespace {

// Voltage divider ratio: 100k/100k divider means ADC sees half the battery voltage
constexpr float VOLTAGE_DIVIDER_RATIO = 2.0f;

// 18650 LiPo discharge curve (simplified linear approximation)
constexpr float BATTERY_VOLTAGE_FULL  = 4.2f;
constexpr float BATTERY_VOLTAGE_EMPTY = 3.0f;

constexpr uint16_t ADC_SAMPLE_COUNT = 16;

esp_adc_cal_characteristics_t adcCharacteristics;

/// Convert battery voltage to percentage using linear approximation.
/// Clamps to 0-100 range, rounds to nearest integer.
uint8_t voltageToPercent(float voltage) {
    if (voltage >= BATTERY_VOLTAGE_FULL) return 100;
    if (voltage <= BATTERY_VOLTAGE_EMPTY) return 0;

    float range = BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY;
    float normalized = (voltage - BATTERY_VOLTAGE_EMPTY) / range;
    return static_cast<uint8_t>(normalized * 100.0f + 0.5f);
}

}  // anonymous namespace

namespace Battery {

bool initialize() {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);

    esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_11,
        ADC_WIDTH_BIT_12,
        1100,  // default Vref in mV
        &adcCharacteristics
    );

    Serial.printf("[BATTERY] ADC initialized on GPIO %u\n", PIN_BATTERY_ADC);
    return true;
}

bool readMeasurements(HivePayload& payload) {
    uint32_t adcSum = 0;

    for (uint16_t i = 0; i < ADC_SAMPLE_COUNT; i++) {
        adcSum += adc1_get_raw(ADC1_CHANNEL_6);
    }

    uint32_t adcAverage = adcSum / ADC_SAMPLE_COUNT;
    uint32_t millivolts = esp_adc_cal_raw_to_voltage(adcAverage, &adcCharacteristics);
    float batteryVoltage = (millivolts / 1000.0f) * VOLTAGE_DIVIDER_RATIO;

    payload.battery_pct = voltageToPercent(batteryVoltage);

    Serial.printf("[BATTERY] Voltage=%.2fV, Percent=%u%%\n",
                  batteryVoltage, payload.battery_pct);
    return true;
}

void enterSleep() {
    // No hardware to power off — ADC pin is passive
}

}  // namespace Battery
