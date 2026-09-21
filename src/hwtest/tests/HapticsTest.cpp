/**
 * HapticsTest.cpp
 *
 * Implementation for HapticsTest.
 */

#include "hwtest/tests/HapticsTest.h"

#include "DisplayColors.h"
#include "HapticsDriver.h"

namespace {
constexpr uint32_t INTRO_MS = 800;
constexpr uint32_t GAP_MS = 1200;

struct HapticStep {
    uint8_t effect_id;
    const char* name;
};

constexpr HapticStep STEPS[] = {
    {HAPTIC_STRONG_CLICK_100, "Strong click"},
    {HAPTIC_DOUBLE_CLICK_100, "Double click"},
    {HAPTIC_STRONG_BUZZ_100, "Strong buzz"},
    {HAPTIC_PULSING_STRONG_1_100, "Pulsing"},
    {HAPTIC_RAMP_UP_LONG_SMOOTH_1_100, "Ramp up"},
};
constexpr uint8_t STEP_COUNT = sizeof(STEPS) / sizeof(STEPS[0]);
constexpr uint8_t INTRO_STEP = 0xFF;
}  // namespace

HapticsTest::HapticsTest(HwTestContext& context)
    : HardwareTest(context)
    , _step(INTRO_STEP)
    , _step_started_ms(0)
    , _all_ok(true)
    , _last_played(false) {
}

void HapticsTest::start(TestScreen& screen) {
    _step = INTRO_STEP;
    _step_started_ms = millis();
    _all_ok = true;
    _last_played = false;
    screen.setField(0, "Driver", COLOR_WHITE, "DRV2605L ERM");
    for (uint8_t i = 0; i < STEP_COUNT; i++) {
        screen.setField(i + 2, STEPS[i].name, COLOR_WHITE, "--");
    }
}

HardwareTest::Result HapticsTest::update(TestScreen& screen) {
    if (!_context.haptics || !_context.haptics->isReady()) {
        screen.setLine(8, COLOR_RED, "DRV2605L not found");
        return Result::FAILED;
    }

    uint32_t now = millis();
    uint32_t hold_ms = (_step == INTRO_STEP) ? INTRO_MS : GAP_MS;
    if (now - _step_started_ms < hold_ms) {
        return Result::RUNNING;
    }

    if (_step != INTRO_STEP && _last_played) {
        screen.updateField(_step + 2, STEPS[_step].name, COLOR_GREEN, "sent");
    }

    _step = (_step == INTRO_STEP) ? 0 : _step + 1;
    _step_started_ms = now;

    if (_step >= STEP_COUNT) {
        screen.setLine(8, _all_ok ? COLOR_CYAN : COLOR_RED,
                       _all_ok ? "Felt all 5? = pass" : "I2C write failed");
        return _all_ok ? Result::PASSED : Result::FAILED;
    }

    bool played = _context.haptics->playEffect(STEPS[_step].effect_id);
    _all_ok = _all_ok && played;
    _last_played = played;
    screen.setField(_step + 2, STEPS[_step].name, played ? COLOR_YELLOW : COLOR_RED, "%s",
                    played ? "playing" : "error");
    return Result::RUNNING;
}

void HapticsTest::finish() {
    if (_context.haptics) {
        _context.haptics->stop();
    }
}
