/**
 * MicTest.h
 *
 * Records a few seconds from the ICS-43434 with a live level meter, reports
 * peak/RMS/clipping, then plays the recording back through the speaker.
 */

#pragma once

#include "hwtest/HardwareTest.h"

class MicTest : public HardwareTest {
public:
    explicit MicTest(HwTestContext& context);
    ~MicTest() override;

    const char* getName() const override { return "MIC"; }
    void start(TestScreen& screen) override;
    Result update(TestScreen& screen) override;
    void finish() override;

private:
    enum class Phase : uint8_t {
        COUNTDOWN,
        RECORDING,
        PLAYBACK,
    };

    void updateRecording(TestScreen& screen);
    Result finishPlayback(TestScreen& screen);
    void freeBuffer();

    Phase _phase;
    uint32_t _phase_started_ms;
    uint32_t _last_draw_ms;
    int16_t* _buffer;
    size_t _capacity_samples;
    size_t _recorded_samples;
    size_t _playback_index;
    int _frame_samples;
    uint8_t _warmup_frames;
    int16_t _peak;
    int16_t _window_peak;    // since the last level-bar draw
    int16_t _progress_peak;  // since the last serial progress line
    uint32_t _last_progress_log_ms;
    uint64_t _sum_squares;
    uint32_t _clip_samples;
};
