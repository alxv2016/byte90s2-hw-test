/**
 * ToneGenerator.h
 *
 * Simple tone generator for UI feedback.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class AudioCodec;

/**
 * @brief ToneGenerator.
 */
class ToneGenerator {
public:
    explicit ToneGenerator(AudioCodec* codec);
    ~ToneGenerator();

    bool playTone(uint16_t freq_hz, uint16_t duration_ms, float volume = 0.3f);

    bool playKeystroke();
    bool playConfirm();
    bool playBackspace();
    bool playTypewriterBell();

private:
    bool lock();
    void unlock();
    int clampSamples(int samples) const;
    bool playToneWithEnvelope(uint16_t freq_hz,
                              uint16_t duration_ms,
                              float volume,
                              uint16_t attack_ms,
                              uint16_t release_ms);

    AudioCodec* _codec;
    SemaphoreHandle_t _mutex;
};
