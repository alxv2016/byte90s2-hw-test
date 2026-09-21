/**
 * DosBootAnimator.cpp
 *
 * DOS-style boot animation using TypingEffect and ToneGenerator.
 */

#include "DosBootAnimator.h"
#include "ToneGenerator.h"
#include "DeviceConfig.h"
#include <esp_heap_caps.h>
#include <esp_system.h>

namespace {
constexpr uint16_t TYPE_DELAY_ULTRA_FAST = 25;
constexpr uint16_t LINE_DELAY_FAST = 120;
constexpr uint16_t PAUSE_ULTRA_SHORT = 250;
constexpr uint16_t PRIMARY_COLOR = COLOR_YELLOW;
constexpr uint16_t ACCENT_COLOR = COLOR_ORANGE;
} // namespace

DosBootAnimator::DosBootAnimator()
    : _display(nullptr)
    , _display_mutex(nullptr)
    , _typing()
    , _tone(nullptr)
    , _running(false)
    , _tint_enabled(false)
    , _tint_color(PRIMARY_COLOR)
{
}

void DosBootAnimator::begin(ArduinoSSD1351* display, SemaphoreHandle_t display_mutex) {
    _display = display;
    _display_mutex = display_mutex;
    _typing.begin(display, display_mutex);
}

void DosBootAnimator::setToneGenerator(ToneGenerator* tone) {
    _tone = tone;
    _typing.setToneGenerator(tone);
}

void DosBootAnimator::setTintColor(uint16_t color, bool enabled) {
    _tint_enabled = enabled;
    _tint_color = color;
}

void DosBootAnimator::runFast() {
    if (_running) {
        return;
    }
    _running = true;
    runFastInternal();
    _running = false;
}

void DosBootAnimator::runFastInternal() {
    if (!_display) {
        return;
    }

    if (lockDisplay()) {
        Adafruit_SSD1351* gfx = _display->getAdafruitDisplay();
        gfx->fillScreen(COLOR_BLACK);
        gfx->setTextSize(1);
        gfx->setTextWrap(false);
        unlockDisplay();
    }

    _typing.setTextSize(1);
    _typing.setCursor(0, 8);

    String chip_model = String(ESP.getChipModel());
    uint32_t cpu_mhz = ESP.getCpuFreqMHz();
    uint32_t flash_mb = ESP.getFlashChipSize() / (1024 * 1024);
    uint32_t psram_mb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / (1024 * 1024);
    String display_model = "SSD1351";

    uint16_t primary_color = _tint_enabled ? _tint_color : PRIMARY_COLOR;
    uint16_t accent_color = _tint_enabled ? _tint_color : ACCENT_COLOR;

    _typing.typeText("BYTE-90 BIOS v2.0", accent_color, TYPE_DELAY_ULTRA_FAST, _tone != nullptr);
    _typing.newLine();
    delay(LINE_DELAY_FAST);

    _typing.typeText("Detecting hardware...", primary_color, TYPE_DELAY_ULTRA_FAST, _tone != nullptr);
    _typing.newLine();
    delay(LINE_DELAY_FAST);

    String mcu_line = "MCU:" + chip_model + "R" + String(flash_mb) + " [OK]";
    _typing.typeText(mcu_line.c_str(), primary_color, TYPE_DELAY_ULTRA_FAST, _tone != nullptr);
    _typing.newLine();

    String cpu_line = "CPU:" + String(cpu_mhz) + "MHz [OK]";
    _typing.typeText(cpu_line.c_str(), primary_color, TYPE_DELAY_ULTRA_FAST, _tone != nullptr);
    _typing.newLine();

    String display_line = "Display:" + display_model + " [OK]";
    _typing.typeText(display_line.c_str(), primary_color, TYPE_DELAY_ULTRA_FAST, _tone != nullptr);
    _typing.newLine();

    String psram_line = psram_mb > 0
        ? "PSRAM:" + String(psram_mb) + "MB [OK]"
        : "PSRAM: None [--]";
    _typing.typeText(psram_line.c_str(), primary_color, TYPE_DELAY_ULTRA_FAST, _tone != nullptr);
    _typing.newLine();
    delay(LINE_DELAY_FAST);

    _typing.typeText("///////////////////", primary_color, TYPE_DELAY_ULTRA_FAST, _tone != nullptr);
    _typing.newLine();
    delay(PAUSE_ULTRA_SHORT);

    String os_line = "BYTE-90 OS v" + String(FIRMWARE_VERSION);
    _typing.typeText(os_line.c_str(), accent_color, TYPE_DELAY_ULTRA_FAST, _tone != nullptr);
    _typing.newLine();
    delay(LINE_DELAY_FAST);

    _typing.typeText("C:\\> run BYTE90.exe", primary_color, TYPE_DELAY_ULTRA_FAST, _tone != nullptr);
    _typing.newLine();
    delay(PAUSE_ULTRA_SHORT);

    if (_tone) {
        _tone->playConfirm();
    }

    if (lockDisplay()) {
        _display->getAdafruitDisplay()->fillScreen(COLOR_BLACK);
        unlockDisplay();
    }
}

bool DosBootAnimator::lockDisplay() {
    if (!_display_mutex) {
        return true;
    }
    return xSemaphoreTake(_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE;
}

void DosBootAnimator::unlockDisplay() {
    if (_display_mutex) {
        xSemaphoreGive(_display_mutex);
    }
}
