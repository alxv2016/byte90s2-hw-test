/**
 * TypingEffect.cpp
 *
 * Reusable typing effect for SSD1351 display.
 */

#include "TypingEffect.h"
#include "ToneGenerator.h"

namespace {
constexpr int CHAR_WIDTH = 6;
constexpr int CHAR_HEIGHT = 10;
constexpr int CURSOR_WIDTH = 6;
constexpr int CURSOR_HEIGHT = 8;
constexpr int CURSOR_BLINK_MS = 120;
} // namespace

TypingEffect::TypingEffect()
    : _display(nullptr)
    , _display_mutex(nullptr)
    , _tone(nullptr)
    , _cursor_x(0)
    , _cursor_y(0)
    , _text_size(1)
{
}

void TypingEffect::begin(ArduinoSSD1351* display, SemaphoreHandle_t display_mutex) {
    _display = display;
    _display_mutex = display_mutex;
    if (_display) {
        _display->getAdafruitDisplay()->setTextWrap(false);
    }
}

void TypingEffect::setToneGenerator(ToneGenerator* tone) {
    _tone = tone;
}

void TypingEffect::setCursor(int16_t x, int16_t y) {
    _cursor_x = x;
    _cursor_y = y;
}

void TypingEffect::setTextSize(uint8_t size) {
    _text_size = size > 0 ? size : 1;
}

void TypingEffect::typeText(const char* text,
                            uint16_t color,
                            uint16_t delay_ms,
                            bool play_sound) {
    if (!_display || !text) {
        return;
    }

    uint32_t sound_index = 0;
    while (*text) {
        char c = *text++;
        if (c == '\n') {
            newLine();
            continue;
        }

        if (play_sound && _tone && (sound_index % 2 == 0)) {
            _tone->playKeystroke();
        }
        sound_index++;

        if (lockDisplay()) {
            Adafruit_SSD1351* gfx = _display->getAdafruitDisplay();
            gfx->setTextSize(_text_size);
            gfx->setTextColor(color);
            gfx->setCursor(_cursor_x, _cursor_y);
            gfx->print(c);
            unlockDisplay();
        }

        _cursor_x += CHAR_WIDTH * _text_size;
        if (_cursor_x >= DISPLAY_WIDTH - (CHAR_WIDTH * _text_size)) {
            newLine();
        }

        delay(delay_ms);
    }
}

void TypingEffect::newLine() {
    _cursor_x = 0;
    _cursor_y += CHAR_HEIGHT * _text_size;
}

void TypingEffect::blinkCursor(uint8_t blinks, uint16_t color, uint16_t background) {
    if (!_display) {
        return;
    }

    for (uint8_t i = 0; i < blinks; i++) {
        if (lockDisplay()) {
            _display->getAdafruitDisplay()->fillRect(
                _cursor_x, _cursor_y, CURSOR_WIDTH, CURSOR_HEIGHT, color);
            unlockDisplay();
        }
        delay(CURSOR_BLINK_MS);

        if (lockDisplay()) {
            _display->getAdafruitDisplay()->fillRect(
                _cursor_x, _cursor_y, CURSOR_WIDTH, CURSOR_HEIGHT, background);
            unlockDisplay();
        }
        delay(CURSOR_BLINK_MS);
    }
}

bool TypingEffect::lockDisplay() {
    if (!_display_mutex) {
        return true;
    }
    return xSemaphoreTake(_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE;
}

void TypingEffect::unlockDisplay() {
    if (_display_mutex) {
        xSemaphoreGive(_display_mutex);
    }
}
