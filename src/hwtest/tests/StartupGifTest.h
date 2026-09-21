/**
 * StartupGifTest.h
 *
 * Power-on GIF test: plays the startup GIF once with the startup sound and
 * reports how playback ended. Runs once after the DOS boot animation.
 */

#pragma once

#include "hwtest/HardwareTest.h"

class StartupGifTest : public HardwareTest {
public:
    explicit StartupGifTest(HwTestContext& context);

    const char* getName() const override { return "STARTUP GIF"; }
    void start(TestScreen& screen) override;
    Result update(TestScreen& screen) override;
    void finish() override;

private:
    enum class Phase : uint8_t {
        INTRO,
        PLAYING,
    };

    Result fail(TestScreen& screen, const char* reason);

    Phase _phase;
    uint32_t _phase_started_ms;
    uint32_t _request_id;
    bool _sound_queued;
};
