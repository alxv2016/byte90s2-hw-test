/**
 * DisplayTest.h
 *
 * Cycles solid fills, a gradient, and an edge/text pattern so dead pixels,
 * color channel faults, and panel offset errors are visible by eye.
 */

#pragma once

#include "hwtest/HardwareTest.h"

class DisplayTest : public HardwareTest {
public:
    explicit DisplayTest(HwTestContext& context);

    const char* getName() const override { return "DISPLAY"; }
    void start(TestScreen& screen) override;
    Result update(TestScreen& screen) override;

private:
    void drawStep(TestScreen& screen, uint8_t step);

    uint8_t _step;
    uint32_t _step_started_ms;
};
