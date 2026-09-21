/**
 * PowerMenu.cpp
 *
 * Implementation for PowerMenu.
 */

#include "hwtest/PowerMenu.h"

#include "ArduinoSSD1351.h"
#include "AudioCodec.h"
#include "Axp2101.h"
#include "DeviceConfig.h"
#include "HapticsDriver.h"

#include <esp_log.h>
#include <string.h>

namespace {
static const char* TAG = "PowerMenu";

constexpr uint16_t COLOR_TITLE_BG = 0x18C3;  // dark grey, matches the page header
constexpr int16_t CHAR_WIDTH = 6;

// Overlay box, centred over the body.
constexpr int16_t BOX_X = 6;
constexpr int16_t BOX_Y = 20;
constexpr int16_t BOX_W = DISPLAY_WIDTH - 2 * BOX_X;
constexpr int16_t BOX_H = 92;
constexpr int16_t TITLE_H = 13;
constexpr int16_t OPTION_X = BOX_X + 6;
constexpr int16_t OPTION_W = BOX_W - 12;
constexpr int16_t OPTION_H = 14;
constexpr int16_t OPTION_Y0 = BOX_Y + TITLE_H + 6;
constexpr int16_t OPTION_PITCH = 17;
constexpr int16_t HINT_Y = BOX_Y + BOX_H - 12;

constexpr const char* OPTION_NAMES[] = {"Cancel", "Restart", "Shutdown"};

// If power is still up this long after asking the PMIC to shut down, it
// did not happen.
constexpr uint32_t SHUTDOWN_GRACE_MS = 2000;
constexpr uint32_t MESSAGE_HOLD_MS = 600;
constexpr uint32_t FAILURE_HOLD_MS = 3000;

int16_t centredX(const char* text, int16_t left, int16_t width) {
    return left + (width - static_cast<int16_t>(strlen(text) * CHAR_WIDTH)) / 2;
}
}  // namespace

PowerMenu::PowerMenu(HwTestContext& context, TestScreen& screen)
    : _context(context)
    , _screen(screen)
    , _open(false)
    , _selected(Option::CANCEL) {
}

void PowerMenu::open() {
    _open = true;
    _selected = Option::CANCEL;
    ESP_LOGI(TAG, "Power menu opened");
    if (_context.haptics) {
        _context.haptics->playEffect(HAPTIC_DOUBLE_CLICK_100);
    }
    draw();
}

void PowerMenu::selectNext() {
    if (!_open) {
        return;
    }
    Option previous = _selected;
    uint8_t next = (static_cast<uint8_t>(_selected) + 1) % static_cast<uint8_t>(Option::COUNT);
    _selected = static_cast<Option>(next);
    ESP_LOGI(TAG, "Highlight: %s", OPTION_NAMES[next]);
    drawOption(previous);
    drawOption(_selected);
}

bool PowerMenu::confirm() {
    if (!_open) {
        return false;
    }
    ESP_LOGI(TAG, "Selected: %s", OPTION_NAMES[static_cast<uint8_t>(_selected)]);

    switch (_selected) {
        case Option::RESTART:
            restart();  // does not return
            break;
        case Option::SHUTDOWN:
            shutdown();  // only returns if power stayed on
            break;
        case Option::CANCEL:
        case Option::COUNT:
            break;
    }
    _open = false;
    return true;
}

void PowerMenu::draw() {
    Adafruit_SSD1351* gfx = _screen.lockGfx();
    if (!gfx) {
        return;
    }
    gfx->fillRect(BOX_X, BOX_Y, BOX_W, BOX_H, COLOR_BLACK);
    gfx->drawRect(BOX_X, BOX_Y, BOX_W, BOX_H, COLOR_WHITE);
    gfx->fillRect(BOX_X + 1, BOX_Y + 1, BOX_W - 2, TITLE_H - 1, COLOR_TITLE_BG);

    gfx->setTextSize(1);
    gfx->setTextWrap(false);
    gfx->setTextColor(COLOR_WHITE);
    const char* title = "Power";
    gfx->setCursor(centredX(title, BOX_X, BOX_W), BOX_Y + 3);
    gfx->print(title);

    const char* hint = "1x next  2x select";
    gfx->setTextColor(COLOR_CYAN);
    gfx->setCursor(centredX(hint, BOX_X, BOX_W), HINT_Y);
    gfx->print(hint);
    _screen.unlockGfx();

    for (uint8_t i = 0; i < static_cast<uint8_t>(Option::COUNT); i++) {
        drawOption(static_cast<Option>(i));
    }
}

void PowerMenu::drawOption(Option option) {
    Adafruit_SSD1351* gfx = _screen.lockGfx();
    if (!gfx) {
        return;
    }

    uint8_t index = static_cast<uint8_t>(option);
    int16_t y = OPTION_Y0 + index * OPTION_PITCH;
    const char* name = OPTION_NAMES[index];
    bool selected = option == _selected;
    bool destructive = option == Option::SHUTDOWN;

    uint16_t fill = COLOR_BLACK;
    uint16_t text = destructive ? COLOR_RED : COLOR_WHITE;
    if (selected) {
        fill = destructive ? COLOR_RED : COLOR_YELLOW;
        text = destructive ? COLOR_WHITE : COLOR_BLACK;
    }

    gfx->fillRect(OPTION_X, y, OPTION_W, OPTION_H, fill);
    if (!selected) {
        gfx->drawRect(OPTION_X, y, OPTION_W, OPTION_H, 0x4208);  // dim outline
    }
    gfx->setTextSize(1);
    gfx->setTextWrap(false);
    gfx->setTextColor(text);
    gfx->setCursor(centredX(name, OPTION_X, OPTION_W), y + 3);
    gfx->print(name);
    _screen.unlockGfx();
}

void PowerMenu::showMessage(const char* line1, const char* line2, uint16_t color) {
    Adafruit_SSD1351* gfx = _screen.lockGfx();
    if (!gfx) {
        return;
    }
    gfx->fillScreen(COLOR_BLACK);
    gfx->setTextSize(1);
    gfx->setTextWrap(false);
    gfx->setTextColor(color);
    gfx->setCursor(centredX(line1, 0, DISPLAY_WIDTH), DISPLAY_HEIGHT / 2 - 10);
    gfx->print(line1);
    if (line2) {
        gfx->setTextColor(COLOR_WHITE);
        gfx->setCursor(centredX(line2, 0, DISPLAY_WIDTH), DISPLAY_HEIGHT / 2 + 4);
        gfx->print(line2);
    }
    _screen.unlockGfx();
}

void PowerMenu::restart() {
    showMessage("Restarting...", nullptr, COLOR_YELLOW);
    delay(MESSAGE_HOLD_MS);
    ESP.restart();
}

void PowerMenu::shutdown() {
    if (!_context.power || !_context.power->isReady()) {
        showMessage("Shutdown failed", "PMIC not ready", COLOR_RED);
        delay(FAILURE_HOLD_MS);
        return;
    }

    showMessage("Shutting down...", "Press key to power on", COLOR_YELLOW);
    delay(MESSAGE_HOLD_MS);
    if (_context.codec) {
        _context.codec->enableOutput(false);
    }
    Adafruit_SSD1351* gfx = _screen.lockGfx();
    if (gfx) {
        gfx->fillScreen(COLOR_BLACK);
        _screen.unlockGfx();
    }

    ESP_LOGI(TAG, "Requesting PMIC power-off");
    _context.power->shutdown();
    delay(SHUTDOWN_GRACE_MS);

    // Still running: the PMIC kept the rail up.
    ESP_LOGW(TAG, "Still powered %lu ms after shutdown request",
             static_cast<unsigned long>(SHUTDOWN_GRACE_MS));
    showMessage("Could not power off", "USB may hold power", COLOR_RED);
    delay(FAILURE_HOLD_MS);
}
