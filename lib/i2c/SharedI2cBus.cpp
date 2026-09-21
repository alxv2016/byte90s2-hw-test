/**
 * @file SharedI2cBus.cpp
 * @brief Implementation of centralized I2C bus management
 */

#include "SharedI2cBus.h"
#include <esp_log.h>

static const char* TAG = "I2C_MANAGER";

SharedI2cBus::SharedI2cBus()
    : _initialized(false)
    , _sda_pin(-1)
    , _scl_pin(-1)
    , _frequency(0) {
}

SharedI2cBus& SharedI2cBus::getInstance() {
    static SharedI2cBus instance;
    return instance;
}

bool SharedI2cBus::begin(int8_t sda_pin, int8_t scl_pin, uint32_t frequency) {
    if (_initialized) {
        ESP_LOGI(TAG, "I2C bus already initialized (SDA=%d, SCL=%d, Freq=%d Hz)",
                 _sda_pin, _scl_pin, _frequency);
        return true;
    }

    _sda_pin = sda_pin;
    _scl_pin = scl_pin;
    _frequency = frequency;

    // Initialize I2C bus
    Wire.begin(_sda_pin, _scl_pin);
    Wire.setClock(_frequency);

    // Small delay to allow bus to stabilize
    delay(10);

    _initialized = true;

    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d, Freq=%d Hz)",
             _sda_pin, _scl_pin, _frequency);

    // Scan for devices
    scanDevices();

    return true;
}

TwoWire* SharedI2cBus::getBus() {
    if (!_initialized) {
        ESP_LOGW(TAG, "I2C bus not initialized - call begin() first");
        return nullptr;
    }
    return &Wire;
}

uint8_t SharedI2cBus::scanDevices() {
    if (!_initialized) {
        ESP_LOGW(TAG, "Cannot scan - I2C bus not initialized");
        return 0;
    }

    ESP_LOGD(TAG, "Scanning I2C bus...");
    uint8_t device_count = 0;

    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();

        if (error == 0) {
            device_count++;

            // Log device with identification
            switch (address) {
                case 0x34:
                    ESP_LOGD(TAG, "  0x%02X - AXP2101 PMIC", address);
                    break;
                case 0x53:
                    ESP_LOGD(TAG, "  0x%02X - ADXL345 Accelerometer", address);
                    break;
                case 0x51:
                    ESP_LOGD(TAG, "  0x%02X - PCF8563 RTC", address);
                    break;
                case 0x5A:
                    ESP_LOGD(TAG, "  0x%02X - DRV2605 Haptic Driver", address);
                    break;
                default:
                    ESP_LOGD(TAG, "  0x%02X - Unknown device", address);
                    break;
            }
        }
    }

    if (device_count == 0) {
        ESP_LOGW(TAG, "No I2C devices found on bus");
    } else {
        ESP_LOGI(TAG, "I2C scan complete - found %d device(s)", device_count);
    }

    return device_count;
}

bool SharedI2cBus::reset() {
    ESP_LOGW(TAG, "Resetting I2C bus...");

    Wire.end();
    delay(100);

    _initialized = false;

    // Re-initialize with same parameters
    return begin(_sda_pin, _scl_pin, _frequency);
}
