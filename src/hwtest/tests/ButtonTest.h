/**
 * ButtonTest.h
 *
 * Verifies the AXP2101 power key in three steps, in order: three slow single
 * clicks, one double click, then one long press (PKEY_LONG IRQ, fires after
 * about 1 s; HwTestRunner buzzes when it does). Holding on to 6 s opens the
 * software power menu instead of powering off, so overshooting is harmless.
 *
 * Clicks arrive already classified by HwTestRunner, so single clicks must be
 * spaced wider than its double-click window to count individually.
 */

#pragma once

#include "hwtest/HardwareTest.h"

class ButtonTest : public HardwareTest {
public:
    explicit ButtonTest(HwTestContext& context);

    const char* getName() const override { return "BUTTON"; }
    void start(TestScreen& screen) override;
    Result update(TestScreen& screen) override;

    bool wantsButton() const override { return true; }
    void onButtonClick() override;
    void onButtonDoubleClick() override;
    void onButtonLongPress() override;

private:
    enum class Step : uint8_t {
        CLICKS,
        DOUBLE_CLICK,
        HOLD,
        COMPLETE,
    };

    void drawProgress(TestScreen& screen);

    uint32_t _started_ms;
    Step _step;
    uint8_t _clicks;
    bool _dirty;
};
