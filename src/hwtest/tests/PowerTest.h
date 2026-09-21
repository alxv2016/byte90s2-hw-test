/**
 * PowerTest.h
 *
 * Probes the shared I2C bus for all four BYTE-90 devices, then checks the
 * AXP2101: DCDC1 rail, system voltage, VBUS, charger state, die temperature.
 */

#pragma once

#include "hwtest/HardwareTest.h"

class PowerTest : public HardwareTest {
public:
    explicit PowerTest(HwTestContext& context);

    const char* getName() const override { return "POWER/I2C"; }
    void start(TestScreen& screen) override;
    Result update(TestScreen& screen) override;

private:
    uint32_t _started_ms;
    bool _done;
};
