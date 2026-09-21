/**
 * ButtonTest.cpp
 *
 * Implementation for ButtonTest.
 */

#include "hwtest/tests/ButtonTest.h"

#include "Axp2101.h"
#include "DisplayColors.h"

namespace {
constexpr uint8_t REQUIRED_CLICKS = 3;
constexpr uint32_t TIMEOUT_MS = 40000;

uint16_t stepColor(bool done, bool active) {
    if (done) {
        return COLOR_GREEN;
    }
    return active ? COLOR_YELLOW : COLOR_WHITE;
}
}  // namespace

ButtonTest::ButtonTest(HwTestContext& context)
    : HardwareTest(context)
    , _started_ms(0)
    , _step(Step::CLICKS)
    , _clicks(0)
    , _dirty(false) {
}

void ButtonTest::start(TestScreen& screen) {
    _started_ms = millis();
    _step = Step::CLICKS;
    _clicks = 0;
    _dirty = false;

    screen.setField(0, "Key", COLOR_WHITE, "AXP2101 IRQ");
    drawProgress(screen);
}

HardwareTest::Result ButtonTest::update(TestScreen& screen) {
    if (!_context.power || !_context.power->isReady()) {
        screen.setLine(8, COLOR_RED, "PMIC not ready");
        return Result::FAILED;
    }

    if (_dirty) {
        _dirty = false;
        drawProgress(screen);
    }

    if (_step == Step::COMPLETE) {
        screen.setLine(8, COLOR_GREEN, "Click/2x/hold OK");
        return Result::PASSED;
    }

    uint32_t elapsed_ms = millis() - _started_ms;
    if (elapsed_ms >= TIMEOUT_MS) {
        screen.setLine(8, COLOR_RED, "Timed out");
        return Result::FAILED;
    }

    screen.updateField(7, "Time left", COLOR_WHITE, "%lu s",
                       static_cast<unsigned long>((TIMEOUT_MS - elapsed_ms) / 1000));
    return Result::RUNNING;
}

void ButtonTest::onButtonClick() {
    // Events out of step order are ignored, so an early double click or a
    // stray click after the hold never counts toward the wrong step.
    if (_step != Step::CLICKS) {
        return;
    }
    _clicks++;
    if (_clicks >= REQUIRED_CLICKS) {
        _step = Step::DOUBLE_CLICK;
    }
    _dirty = true;
}

void ButtonTest::onButtonDoubleClick() {
    if (_step == Step::DOUBLE_CLICK) {
        _step = Step::HOLD;
        _dirty = true;
    }
}

void ButtonTest::onButtonLongPress() {
    if (_step == Step::HOLD) {
        _step = Step::COMPLETE;
        _dirty = true;
    }
}

void ButtonTest::drawProgress(TestScreen& screen) {
    bool clicks_done = _step > Step::CLICKS;
    bool double_done = _step > Step::DOUBLE_CLICK;
    bool hold_done = _step > Step::HOLD;

    screen.setField(2, "1) Slow clicks", stepColor(clicks_done, _step == Step::CLICKS),
                    "%u/%u", _clicks, REQUIRED_CLICKS);
    screen.setField(3, "2) Double-click", stepColor(double_done, _step == Step::DOUBLE_CLICK),
                    "%s", double_done ? "OK" : "--");
    screen.setField(4, "3) Hold to buzz", stepColor(hold_done, _step == Step::HOLD),
                    "%s", hold_done ? "OK" : "--");
    screen.updateLine(5, COLOR_CYAN, "%s", _step == Step::HOLD ? "   then let go" : "");
}
