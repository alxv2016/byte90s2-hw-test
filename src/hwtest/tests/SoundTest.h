/**
 * SoundTest.h
 *
 * Drives the MAX98357A through the full-duplex I2S port: three square-wave
 * tones, then an MP3 decoded from LittleFS.
 */

#pragma once

#include "hwtest/HardwareTest.h"

class SoundTest : public HardwareTest {
public:
    explicit SoundTest(HwTestContext& context);

    const char* getName() const override { return "SOUND"; }
    void start(TestScreen& screen) override;
    Result update(TestScreen& screen) override;
    void finish() override;

private:
    void drawToneRow(TestScreen& screen, uint8_t index, uint16_t color, const char* state);

    uint8_t _step;
    uint32_t _step_started_ms;
    bool _all_ok;
    bool _mp3_queued;
};
