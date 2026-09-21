/**
 * WifiTest.h
 *
 * Brings up the radio in station mode, scans for networks, and, when
 * credentials are stored in NVS, joins the network and starts NTP so the
 * RTC test can write real time. The connection is left up afterwards.
 */

#pragma once

#include "hwtest/HardwareTest.h"

class WifiTest : public HardwareTest {
public:
    explicit WifiTest(HwTestContext& context);

    const char* getName() const override { return "WIFI"; }
    void start(TestScreen& screen) override;
    Result update(TestScreen& screen) override;

private:
    enum class Phase : uint8_t {
        SCANNING,
        CONNECTING,
    };

    Result showScanResults(TestScreen& screen, int16_t count);
    Result finishConnect(TestScreen& screen, bool connected);

    Phase _phase;
    uint32_t _phase_started_ms;
    uint32_t _last_draw_ms;
    bool _scan_ok;
};
