/**
 * DosBootAnimator.h
 *
 * DOS-style boot animation using TypingEffect and ToneGenerator.
 */

#pragma once

#include "ArduinoSSD1351.h"
#include "TypingEffect.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class ToneGenerator;

/**
 * @brief DosBootAnimator.
 */
class DosBootAnimator {
public:
    DosBootAnimator();

    void begin(ArduinoSSD1351* display, SemaphoreHandle_t display_mutex);
    void setToneGenerator(ToneGenerator* tone);
    void setTintColor(uint16_t color, bool enabled);

    void runFast();
    bool isRunning() const { return _running; }

private:
    void runFastInternal();

    bool lockDisplay();
    void unlockDisplay();

    ArduinoSSD1351* _display;
    SemaphoreHandle_t _display_mutex;
    TypingEffect _typing;
    ToneGenerator* _tone;
    bool _running;
    bool _tint_enabled;
    uint16_t _tint_color;
};
