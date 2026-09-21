/**
 * ArduinoSSD1351.h
 *
 * Declarations for ArduinoSSD1351.
 */

#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>

#include "DisplayColors.h"

#define DISPLAY_BRIGHTNESS_DIM     0x00
#define DISPLAY_BRIGHTNESS_LOW     0x02
#define DISPLAY_BRIGHTNESS_MEDIUM  0x05
#define DISPLAY_BRIGHTNESS_HIGH    0x07
#define DISPLAY_BRIGHTNESS_FULL    0x0F

/**
 * @brief ArduinoSSD1351.
 */
class ArduinoSSD1351 {
public:
    ArduinoSSD1351(int8_t cs_pin, int8_t dc_pin, int8_t rst_pin,
                   int8_t sclk_pin, int8_t mosi_pin);
    
    /**
     * @brief Initialize the display
     */
    bool begin();
    
    /**
     * @brief Set display brightness level
     * @param brightness Level 0-15 (use DISPLAY_BRIGHTNESS_* constants)
     */
    void setBrightness(uint8_t brightness);
    
    /**
     * @brief Set display brightness as percentage
     * @param percent Brightness level 0-100%
     */
    void setBrightnessPercent(uint8_t percent);
    
    /**
     * @brief Get current brightness as percentage
     */
    uint8_t getBrightnessPercent() const;
    
    // Direct access to Adafruit display for all other functions
    Adafruit_SSD1351* operator->() { return &_display; }
    Adafruit_SSD1351& operator*() { return _display; }
    Adafruit_SSD1351* getAdafruitDisplay() { return &_display; }

private:
    Adafruit_SSD1351 _display;
    uint8_t _brightness;
};
