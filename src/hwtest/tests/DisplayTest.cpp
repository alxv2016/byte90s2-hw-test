/**
 * DisplayTest.cpp
 *
 * Implementation for DisplayTest.
 */

#include "hwtest/tests/DisplayTest.h"

#include "ArduinoSSD1351.h"
#include "DeviceConfig.h"

#include <esp_log.h>

namespace {
static const char* TAG = "DisplayTest";
constexpr uint32_t INTRO_MS = 1000;
constexpr uint32_t STEP_MS = 700;
constexpr uint32_t PATTERN_STEP_MS = 2000;

struct FillStep {
    uint16_t color;
    const char* name;
};

constexpr FillStep FILL_STEPS[] = {
    {COLOR_RED, "red"},
    {COLOR_GREEN, "green"},
    {COLOR_BLUE, "blue"},
    {COLOR_WHITE, "white"},
    {COLOR_BLACK, "black"},
};
constexpr uint8_t FILL_STEP_COUNT = sizeof(FILL_STEPS) / sizeof(FILL_STEPS[0]);
constexpr uint8_t GRADIENT_STEP = FILL_STEP_COUNT;
constexpr uint8_t PATTERN_STEP = FILL_STEP_COUNT + 1;
constexpr uint8_t STEP_COUNT = FILL_STEP_COUNT + 2;
constexpr uint8_t INTRO_STEP = 0xFF;
}  // namespace

DisplayTest::DisplayTest(HwTestContext& context)
    : HardwareTest(context)
    , _step(INTRO_STEP)
    , _step_started_ms(0) {
}

void DisplayTest::start(TestScreen& screen) {
    _step = INTRO_STEP;
    _step_started_ms = millis();
    screen.setField(0, "Panel", COLOR_WHITE, "SSD1351 128x128");
    screen.setLine(2, COLOR_CYAN, "Fills: R G B W K");
    screen.setLine(3, COLOR_CYAN, "Then gradient and");
    screen.setLine(4, COLOR_CYAN, "edge/text pattern");
    screen.setLine(6, COLOR_WHITE, "Watch for dead px");
}

HardwareTest::Result DisplayTest::update(TestScreen& screen) {
    if (!_context.display) {
        screen.setLine(8, COLOR_RED, "Display not ready");
        return Result::FAILED;
    }

    uint32_t now = millis();
    uint32_t hold_ms = INTRO_MS;
    if (_step != INTRO_STEP) {
        hold_ms = _step == PATTERN_STEP ? PATTERN_STEP_MS : STEP_MS;
    }
    if (now - _step_started_ms < hold_ms) {
        return Result::RUNNING;
    }

    _step = (_step == INTRO_STEP) ? 0 : _step + 1;
    _step_started_ms = now;

    if (_step < STEP_COUNT) {
        drawStep(screen, _step);
        return Result::RUNNING;
    }

    screen.redrawFrame();
    screen.setField(0, "Fills", COLOR_WHITE, "R G B W K");
    screen.setField(1, "Gradient", COLOR_WHITE, "shown");
    screen.setField(2, "Edges/text", COLOR_WHITE, "shown");
    screen.setLine(4, COLOR_CYAN, "Pass = no dead px,");
    screen.setLine(5, COLOR_CYAN, "even colors, and");
    screen.setLine(6, COLOR_CYAN, "border on all edges");
    return Result::PASSED;
}

void DisplayTest::drawStep(TestScreen& screen, uint8_t step) {
    Adafruit_SSD1351* gfx = screen.lockGfx();
    if (!gfx) {
        return;
    }

    if (step < FILL_STEP_COUNT) {
        gfx->fillScreen(FILL_STEPS[step].color);
        screen.unlockGfx();
        ESP_LOGI(TAG, "  fill %s", FILL_STEPS[step].name);
        return;
    }

    if (step == GRADIENT_STEP) {
        // Four horizontal bands: red, green, blue and grey ramps left to right.
        const int16_t band = DISPLAY_HEIGHT / 4;
        for (int16_t x = 0; x < DISPLAY_WIDTH; x++) {
            uint8_t level = (x * 255) / (DISPLAY_WIDTH - 1);
            uint16_t r = (level >> 3) << 11;
            uint16_t g = (level >> 2) << 5;
            uint16_t b = level >> 3;
            gfx->drawFastVLine(x, 0, band, r);
            gfx->drawFastVLine(x, band, band, g);
            gfx->drawFastVLine(x, band * 2, band, b);
            gfx->drawFastVLine(x, band * 3, band, r | g | b);
        }
        screen.unlockGfx();
        ESP_LOGI(TAG, "  gradient");
        return;
    }

    // Edge and text pattern: 1 px border, corner marks, centre cross, text sizes.
    gfx->fillScreen(COLOR_BLACK);
    gfx->drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_WHITE);
    gfx->fillRect(0, 0, 4, 4, COLOR_RED);
    gfx->fillRect(DISPLAY_WIDTH - 4, 0, 4, 4, COLOR_GREEN);
    gfx->fillRect(0, DISPLAY_HEIGHT - 4, 4, 4, COLOR_BLUE);
    gfx->fillRect(DISPLAY_WIDTH - 4, DISPLAY_HEIGHT - 4, 4, 4, COLOR_YELLOW);
    gfx->drawFastHLine(DISPLAY_WIDTH / 2 - 6, DISPLAY_HEIGHT / 2, 13, COLOR_CYAN);
    gfx->drawFastVLine(DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 6, 13, COLOR_CYAN);

    gfx->setTextWrap(false);
    gfx->setTextColor(COLOR_WHITE);
    gfx->setTextSize(1);
    gfx->setCursor(8, 10);
    gfx->print("Size 1 text");
    gfx->setTextSize(2);
    gfx->setCursor(8, 24);
    gfx->print("Size 2");
    gfx->setTextSize(3);
    gfx->setTextColor(COLOR_ORANGE);
    gfx->setCursor(8, 82);
    gfx->print("BYTE");
    screen.unlockGfx();
    ESP_LOGI(TAG, "  edge/text pattern");
}
