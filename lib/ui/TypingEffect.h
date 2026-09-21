/**
 * TypingEffect.h
 *
 * Reusable typing effect for SSD1351 display.
 */

#pragma once

#include "ArduinoSSD1351.h"
#include "DeviceConfig.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class ToneGenerator;

/**
 * @brief TypingEffect.
 */
class TypingEffect {
public:
    TypingEffect();

    void begin(ArduinoSSD1351* display, SemaphoreHandle_t display_mutex);
    void setToneGenerator(ToneGenerator* tone);
    void setCursor(int16_t x, int16_t y);
    void setTextSize(uint8_t size);

    void typeText(const char* text,
                  uint16_t color,
                  uint16_t delay_ms,
                  bool play_sound = true);
    void newLine();
    void blinkCursor(uint8_t blinks, uint16_t color, uint16_t background);

private:
    void printChar(char c);
    bool lockDisplay();
    void unlockDisplay();

    ArduinoSSD1351* _display;
    SemaphoreHandle_t _display_mutex;
    ToneGenerator* _tone;
    int16_t _cursor_x;
    int16_t _cursor_y;
    uint8_t _text_size;
};
