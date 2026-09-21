/**
 * RtcTest.h
 *
 * Reads the PCF8563, checks the oscillator is ticking, and, if WiFi brought
 * NTP time, writes it to the RTC and reads it back.
 */

#pragma once

#include "hwtest/HardwareTest.h"

#include <time.h>

class RtcTest : public HardwareTest {
public:
    explicit RtcTest(HwTestContext& context);

    const char* getName() const override { return "RTC"; }
    void start(TestScreen& screen) override;
    Result update(TestScreen& screen) override;

private:
    enum class Phase : uint8_t {
        TICKING,
        WAIT_NTP,
    };

    Result finishTicking(TestScreen& screen);
    Result finishNtp(TestScreen& screen, time_t ntp_now);

    Phase _phase;
    uint32_t _phase_started_ms;
    uint32_t _first_unix;
    bool _lost_power;
    bool _ticking;
};
