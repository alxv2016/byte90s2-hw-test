/**
 * BatteryInfoTest.h
 *
 * Battery readout from the AXP2101 fuel gauge, taken after the WiFi test so
 * it reflects the board under load, followed by a USB unplug/replug check.
 *
 *   1. Sample for a few seconds: level, cell voltage, USB, charger state, and
 *      the cell-voltage trend.
 *   2. Ask the operator to toggle USB twice (unplug then replug, or the
 *      reverse if the board started on battery) and confirm the PMIC sees
 *      each change. The step that moves onto battery reports the voltage
 *      sag; the step onto USB reports whether charging resumed.
 *
 * The AXP2101 cannot measure current. "Charge" shows the charger phase and
 * the configured current limit; discharge is shown as state and voltage.
 *
 * The test fails before step 2 when no battery is present, so it never asks
 * the operator to unplug a board that would lose power.
 */

#pragma once

#include "hwtest/HardwareTest.h"

struct PowerTelemetry;

class BatteryInfoTest : public HardwareTest {
public:
    explicit BatteryInfoTest(HwTestContext& context);

    const char* getName() const override { return "BATTERY"; }
    void start(TestScreen& screen) override;
    Result update(TestScreen& screen) override;

private:
    enum class Phase : uint8_t {
        SAMPLING,
        WAIT_FIRST_TOGGLE,
        SETTLE_FIRST_TOGGLE,
        WAIT_SECOND_TOGGLE,
        SETTLE_SECOND_TOGGLE,
    };

    void drawLive(TestScreen& screen, const PowerTelemetry& telemetry, bool log);
    void reportToggle(TestScreen& screen, uint8_t row, const PowerTelemetry& telemetry);
    Result finish(TestScreen& screen);

    Phase _phase;
    uint32_t _phase_started_ms;
    uint32_t _last_draw_ms;
    bool _usb_at_start;
    uint16_t _first_mv;
    uint16_t _min_mv;
    uint16_t _max_mv;
    uint16_t _last_mv_on_usb;
};
