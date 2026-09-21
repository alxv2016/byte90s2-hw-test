/**
 * HapticsTest.h
 *
 * Plays a short sequence of DRV2605L ROM effects on the ERM motor. The
 * driver can only confirm the I2C writes; the operator confirms by feel.
 */

#pragma once

#include "hwtest/HardwareTest.h"

class HapticsTest : public HardwareTest {
public:
    explicit HapticsTest(HwTestContext& context);

    const char* getName() const override { return "HAPTICS"; }
    void start(TestScreen& screen) override;
    Result update(TestScreen& screen) override;
    void finish() override;

private:
    uint8_t _step;
    uint32_t _step_started_ms;
    bool _all_ok;
    bool _last_played;
};
