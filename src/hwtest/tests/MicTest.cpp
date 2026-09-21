/**
 * MicTest.cpp
 *
 * Implementation for MicTest.
 */

#include "hwtest/tests/MicTest.h"

#include "AudioCodec.h"
#include "DeviceConfig.h"
#include "DisplayColors.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <math.h>

namespace {
static const char* TAG = "MicTest";
constexpr uint32_t COUNTDOWN_MS = 1500;
constexpr uint32_t RECORD_MS = 3000;
constexpr uint32_t DRAW_INTERVAL_MS = 100;
constexpr uint32_t PROGRESS_LOG_MS = 500;
constexpr uint32_t FRAME_MS = 20;
constexpr uint8_t WARMUP_FRAMES = 3;
constexpr int16_t CLIP_THRESHOLD = 32760;

// Peak below this over the whole take means the mic path is dead.
constexpr int16_t SILENT_PEAK = 200;
// Peak below this means signal exists but the operator likely did not speak.
constexpr int16_t QUIET_PEAK = 3000;

constexpr uint8_t LEVEL_ROW = 3;
}  // namespace

MicTest::MicTest(HwTestContext& context)
    : HardwareTest(context)
    , _phase(Phase::COUNTDOWN)
    , _phase_started_ms(0)
    , _last_draw_ms(0)
    , _buffer(nullptr)
    , _capacity_samples(0)
    , _recorded_samples(0)
    , _playback_index(0)
    , _frame_samples(0)
    , _warmup_frames(0)
    , _peak(0)
    , _window_peak(0)
    , _progress_peak(0)
    , _last_progress_log_ms(0)
    , _sum_squares(0)
    , _clip_samples(0) {
}

MicTest::~MicTest() {
    freeBuffer();
}

void MicTest::start(TestScreen& screen) {
    _phase = Phase::COUNTDOWN;
    _phase_started_ms = millis();
    _last_draw_ms = 0;
    _recorded_samples = 0;
    _playback_index = 0;
    _warmup_frames = WARMUP_FRAMES;
    _peak = 0;
    _window_peak = 0;
    _progress_peak = 0;
    _last_progress_log_ms = 0;
    _sum_squares = 0;
    _clip_samples = 0;

    int sample_rate = _context.codec ? _context.codec->getActualInputSampleRate() : 0;
    if (sample_rate <= 0) {
        sample_rate = AUDIO_INPUT_SAMPLE_RATE;
    }
    _frame_samples = sample_rate * FRAME_MS / 1000;
    _capacity_samples = static_cast<size_t>(sample_rate) * RECORD_MS / 1000;

    if (!_buffer) {
        _buffer = static_cast<int16_t*>(
            heap_caps_malloc(_capacity_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    }

    screen.setField(0, "Mic", COLOR_WHITE, "ICS-43434 16kHz");
    screen.setLine(1, COLOR_CYAN, "Speak after countdown");
}

HardwareTest::Result MicTest::update(TestScreen& screen) {
    if (!_context.codec || !_context.codec->isReady()) {
        screen.setLine(8, COLOR_RED, "Audio codec not ready");
        return Result::FAILED;
    }
    if (!_buffer) {
        screen.setLine(8, COLOR_RED, "No PSRAM for buffer");
        return Result::FAILED;
    }

    uint32_t now = millis();

    switch (_phase) {
        case Phase::COUNTDOWN: {
            uint32_t elapsed_ms = now - _phase_started_ms;
            if (elapsed_ms < COUNTDOWN_MS) {
                screen.updateField(2, "Recording in", COLOR_YELLOW, "%lu",
                                  static_cast<unsigned long>((COUNTDOWN_MS - elapsed_ms) / 500 + 1));
                return Result::RUNNING;
            }
            _context.codec->enableInput(true);
            _phase = Phase::RECORDING;
            _phase_started_ms = now;
            _last_progress_log_ms = now;
            ESP_LOGI(TAG, "Recording: %u samples, frame %d, gain %.1fx",
                     static_cast<unsigned>(_capacity_samples), _frame_samples,
                     _context.codec->getInputGain());
            screen.setLine(2, COLOR_YELLOW, "> Recording 3 s");
            return Result::RUNNING;
        }

        case Phase::RECORDING:
            updateRecording(screen);
            if (_recorded_samples < _capacity_samples &&
                now - _phase_started_ms < RECORD_MS + 1000) {
                return Result::RUNNING;
            }
            ESP_LOGI(TAG, "Recorded %u samples in %lu ms, peak %d",
                     static_cast<unsigned>(_recorded_samples),
                     static_cast<unsigned long>(now - _phase_started_ms), static_cast<int>(_peak));
            _phase = Phase::PLAYBACK;
            _phase_started_ms = now;
            _playback_index = 0;
            _context.codec->enableOutput(true);
            screen.setLine(2, COLOR_YELLOW, "> Playing back");
            return Result::RUNNING;

        case Phase::PLAYBACK: {
            if (_playback_index >= _recorded_samples) {
                ESP_LOGI(TAG, "Playback done in %lu ms",
                         static_cast<unsigned long>(now - _phase_started_ms));
                return finishPlayback(screen);
            }
            size_t chunk = min(_recorded_samples - _playback_index,
                               static_cast<size_t>(_frame_samples));
            // Blocks roughly one frame while I2S drains, which paces playback.
            _context.codec->write(_buffer + _playback_index, static_cast<int>(chunk));
            _playback_index += chunk;
            return Result::RUNNING;
        }
    }
    return Result::RUNNING;
}

void MicTest::updateRecording(TestScreen& screen) {
    int16_t frame[512];
    int to_read = min(_frame_samples, static_cast<int>(sizeof(frame) / sizeof(frame[0])));
    int samples_read = _context.codec->read(frame, to_read);
    if (samples_read <= 0) {
        return;
    }

    // The ICS-43434 needs a few frames to settle after the clocks start.
    if (_warmup_frames > 0) {
        _warmup_frames--;
        return;
    }

    size_t space = _capacity_samples - _recorded_samples;
    size_t count = min(static_cast<size_t>(samples_read), space);
    for (size_t i = 0; i < count; i++) {
        int16_t sample = frame[i];
        int16_t magnitude = sample == INT16_MIN ? INT16_MAX : abs(sample);
        _peak = max(_peak, magnitude);
        _window_peak = max(_window_peak, magnitude);
        _progress_peak = max(_progress_peak, magnitude);
        if (magnitude >= CLIP_THRESHOLD) {
            _clip_samples++;
        }
        _sum_squares += static_cast<int64_t>(sample) * sample;
        _buffer[_recorded_samples + i] = sample;
    }
    _recorded_samples += count;

    uint32_t now = millis();
    if (now - _last_draw_ms >= DRAW_INTERVAL_MS) {
        _last_draw_ms = now;
        uint8_t percent = static_cast<uint8_t>(min<int32_t>(100, _window_peak * 100L / 16384));
        uint16_t color = _window_peak >= CLIP_THRESHOLD ? COLOR_RED : COLOR_GREEN;
        screen.drawBar(LEVEL_ROW, percent, color);
        screen.updateField(LEVEL_ROW + 1, "Level", COLOR_WHITE, "%d  %.1f s",
                           static_cast<int>(_window_peak), (now - _phase_started_ms) / 1000.0f);
        _window_peak = 0;
    }
    if (now - _last_progress_log_ms >= PROGRESS_LOG_MS) {
        _last_progress_log_ms = now;
        ESP_LOGI(TAG, "  %.1f s: peak %d", (now - _phase_started_ms) / 1000.0f,
                 static_cast<int>(_progress_peak));
        _progress_peak = 0;
    }
}

HardwareTest::Result MicTest::finishPlayback(TestScreen& screen) {
    float rms = _recorded_samples > 0
                    ? sqrtf(static_cast<float>(_sum_squares) / _recorded_samples)
                    : 0.0f;
    float clip_pct = _recorded_samples > 0
                         ? _clip_samples * 100.0f / _recorded_samples
                         : 0.0f;

    screen.clearBody();
    screen.setField(0, "Samples", COLOR_WHITE, "%u", static_cast<unsigned>(_recorded_samples));
    screen.setField(1, "Peak", _peak < SILENT_PEAK ? COLOR_RED : COLOR_WHITE, "%d",
                    static_cast<int>(_peak));
    screen.setField(2, "RMS", COLOR_WHITE, "%.0f", rms);
    screen.setField(3, "Clip", clip_pct >= 0.5f ? COLOR_RED : COLOR_WHITE, "%.2f%%", clip_pct);
    screen.setField(4, "Gain", COLOR_WHITE, "%.1fx", _context.codec->getInputGain());

    if (_recorded_samples == 0) {
        screen.setLine(6, COLOR_RED, "No samples from I2S");
        return Result::FAILED;
    }
    if (_peak < SILENT_PEAK) {
        screen.setLine(6, COLOR_RED, "Mic silent / dead");
        return Result::FAILED;
    }
    if (_peak < QUIET_PEAK) {
        screen.setLine(6, COLOR_YELLOW, "Quiet: speak louder");
    } else if (clip_pct >= 0.5f) {
        screen.setLine(6, COLOR_YELLOW, "Hot: lower gain");
    }
    screen.setLine(7, COLOR_CYAN, "Heard playback? pass");
    return Result::PASSED;
}

void MicTest::finish() {
    if (_context.codec) {
        _context.codec->enableInput(false);
    }
    freeBuffer();
}

void MicTest::freeBuffer() {
    if (_buffer) {
        heap_caps_free(_buffer);
        _buffer = nullptr;
    }
}
