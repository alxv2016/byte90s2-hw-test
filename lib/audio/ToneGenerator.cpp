/**
 * ToneGenerator.cpp
 *
 * Simple tone generator for UI feedback.
 */

#include "ToneGenerator.h"
#include "AudioCodec.h"
#include <algorithm>
#include <math.h>

namespace {
constexpr int MAX_CHUNK_SAMPLES = 960; // 10ms at 96kHz
constexpr int SFX_CHUNK_SAMPLES = 256;
constexpr float SFX_MASTER_VOLUME = 0.20f;

} // namespace

static float clamp01(float value) {
    return std::max(0.0f, std::min(value, 1.0f));
}

static float toScaledVolume(uint8_t volume) {
    float base = static_cast<float>(volume) / 100.0f;
    return clamp01(base * SFX_MASTER_VOLUME);
}

ToneGenerator::ToneGenerator(AudioCodec* codec)
    : _codec(codec)
    , _mutex(xSemaphoreCreateMutex())
{
}

ToneGenerator::~ToneGenerator() {
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

bool ToneGenerator::lock() {
    if (!_mutex) {
        return true;
    }
    return xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE;
}

void ToneGenerator::unlock() {
    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
}

int ToneGenerator::clampSamples(int samples) const {
    return std::max(1, std::min(samples, MAX_CHUNK_SAMPLES));
}

bool ToneGenerator::playTone(uint16_t freq_hz, uint16_t duration_ms, float volume) {
    return playToneWithEnvelope(freq_hz, duration_ms, volume, 0, 0);
}

bool ToneGenerator::playToneWithEnvelope(uint16_t freq_hz,
                                         uint16_t duration_ms,
                                         float volume,
                                         uint16_t attack_ms,
                                         uint16_t release_ms) {
    if (!_codec || freq_hz == 0 || duration_ms == 0) {
        return false;
    }
    if (!lock()) {
        return false;
    }

    int sample_rate = _codec->getOutputSampleRate();
    if (sample_rate <= 0) {
        unlock();
        return false;
    }

    _codec->enableOutput(true);

    volume = std::max(0.0f, std::min(volume, 1.0f));
    int total_samples = (sample_rate * duration_ms) / 1000;
    if (total_samples <= 0) {
        unlock();
        return false;
    }

    int chunk_samples = clampSamples(SFX_CHUNK_SAMPLES);
    int attack_samples = attack_ms > 0
        ? std::max(1, (sample_rate * static_cast<int>(attack_ms)) / 1000)
        : 0;
    int release_samples = release_ms > 0
        ? std::max(1, (sample_rate * static_cast<int>(release_ms)) / 1000)
        : 0;
    float phase = 0.0f;
    float phase_step = static_cast<float>(freq_hz) / static_cast<float>(sample_rate);
    int16_t buffer[MAX_CHUNK_SAMPLES];

    int produced = 0;
    while (produced < total_samples) {
        int remaining = total_samples - produced;
        int take = std::min(remaining, chunk_samples);

        for (int i = 0; i < take; i++) {
            float env = 1.0f;
            if (attack_samples > 0 && produced + i < attack_samples) {
                env = static_cast<float>(produced + i) / static_cast<float>(attack_samples);
            } else if (release_samples > 0 && produced + i > total_samples - release_samples) {
                int rel = total_samples - (produced + i);
                env = static_cast<float>(rel) / static_cast<float>(release_samples);
            }

            phase += phase_step;
            if (phase >= 1.0f) {
                phase -= 1.0f;
            }

            float amp = (phase < 0.5f ? 1.0f : -1.0f) * volume * env;
            int32_t s = static_cast<int32_t>(amp * 32767.0f);
            buffer[i] = static_cast<int16_t>(std::clamp<int32_t>(s, -32768, 32767));
        }

        _codec->write(buffer, take);
        produced += take;
        _codec->keepOutputAlive();
    }

    unlock();
    return true;
}

bool ToneGenerator::playKeystroke() {
    return playToneWithEnvelope(5000, 18, toScaledVolume(60), 1, 1);
}

bool ToneGenerator::playConfirm() {
    bool ok = playTone(1000, 100, toScaledVolume(40));
    delay(50);
    return ok && playTone(1300, 150, toScaledVolume(40));
}

bool ToneGenerator::playBackspace() {
    return playTone(800, 40, toScaledVolume(40));
}

bool ToneGenerator::playTypewriterBell() {
    bool ok = playTone(2000, 200, toScaledVolume(60));
    delay(50);
    return ok && playTone(2000, 100, toScaledVolume(40));
}
