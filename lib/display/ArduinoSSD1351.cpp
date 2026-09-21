/**
 * ArduinoSSD1351.cpp
 *
 * Implementation for ArduinoSSD1351.
 */

#include "ArduinoSSD1351.h"
#include <esp_log.h>

#define SSD1351_CMD_CONTRASTMASTER 0xC7

static const char* TAG = "ArduinoSSD1351";

ArduinoSSD1351::ArduinoSSD1351(int8_t cs_pin, int8_t dc_pin, int8_t rst_pin,
                               int8_t sclk_pin, int8_t mosi_pin)
    : _display(128, 128, &SPI, cs_pin, dc_pin, rst_pin), _brightness(DISPLAY_BRIGHTNESS_FULL) {
    SPI.begin(sclk_pin, -1, mosi_pin, cs_pin);
}

bool ArduinoSSD1351::begin() {
    ESP_LOGI(TAG, "Initializing...");
    _display.begin(18000000);
    setBrightness(_brightness);
    ESP_LOGI(TAG, "✅  Display initialized");
    return true;
}

void ArduinoSSD1351::setBrightness(uint8_t brightness) {
    if (brightness > DISPLAY_BRIGHTNESS_FULL) brightness = DISPLAY_BRIGHTNESS_FULL;
    _brightness = brightness;
    _display.sendCommand(SSD1351_CMD_CONTRASTMASTER, &brightness, 1);
}

void ArduinoSSD1351::setBrightnessPercent(uint8_t percent) {
    uint8_t brightness = map(percent, 0, 100, 0, DISPLAY_BRIGHTNESS_FULL);
    setBrightness(brightness);
}

uint8_t ArduinoSSD1351::getBrightnessPercent() const {
    return map(_brightness, 0, DISPLAY_BRIGHTNESS_FULL, 0, 100);
}