/**
 * SoundTest.cpp
 *
 * Implementation for SoundTest.
 */

#include "hwtest/tests/SoundTest.h"

#include "AudioCodec.h"
#include "DisplayColors.h"
#include "Mp3Player.h"
#include "ToneGenerator.h"

namespace {
constexpr uint32_t INTRO_MS = 800;
constexpr uint32_t TONE_GAP_MS = 250;
constexpr uint32_t MP3_TIMEOUT_MS = 6000;
constexpr uint16_t TONE_MS = 400;
constexpr float TONE_VOLUME = 0.15f;
constexpr const char* MP3_PATH = "/sounds/ding.mp3";

constexpr uint16_t TONES_HZ[] = {440, 1000, 2000};
constexpr uint8_t TONE_COUNT = sizeof(TONES_HZ) / sizeof(TONES_HZ[0]);
constexpr uint8_t MP3_STEP = TONE_COUNT;
constexpr uint8_t INTRO_STEP = 0xFF;
}  // namespace

SoundTest::SoundTest(HwTestContext& context)
    : HardwareTest(context)
    , _step(INTRO_STEP)
    , _step_started_ms(0)
    , _all_ok(true)
    , _mp3_queued(false) {
}

void SoundTest::start(TestScreen& screen) {
    _step = INTRO_STEP;
    _step_started_ms = millis();
    _all_ok = true;
    _mp3_queued = false;

    screen.setField(0, "Amp", COLOR_WHITE, "MAX98357A 16kHz");
    for (uint8_t i = 0; i < TONE_COUNT; i++) {
        drawToneRow(screen, i, COLOR_WHITE, "--");
    }
    screen.setField(MP3_STEP + 2, "MP3 ding.mp3", COLOR_WHITE, "--");
}

HardwareTest::Result SoundTest::update(TestScreen& screen) {
    if (!_context.codec || !_context.codec->isReady() || !_context.tone) {
        screen.setLine(8, COLOR_RED, "Audio codec not ready");
        return Result::FAILED;
    }

    uint32_t now = millis();

    if (_step == MP3_STEP) {
        bool playing = _context.mp3 && _context.mp3->isPlaying();
        bool timed_out = now - _step_started_ms >= MP3_TIMEOUT_MS;
        if (playing && !timed_out) {
            return Result::RUNNING;
        }
        bool mp3_ok = _mp3_queued && !timed_out;
        _all_ok = _all_ok && mp3_ok;
        screen.setField(MP3_STEP + 2, "MP3 ding.mp3", mp3_ok ? COLOR_GREEN : COLOR_RED, "%s",
                        mp3_ok ? "OK" : (timed_out ? "hung" : "error"));
        screen.setLine(8, _all_ok ? COLOR_CYAN : COLOR_RED,
                       _all_ok ? "Heard all 4? = pass" : "Playback error");
        return _all_ok ? Result::PASSED : Result::FAILED;
    }

    uint32_t hold_ms = (_step == INTRO_STEP) ? INTRO_MS : TONE_GAP_MS;
    if (now - _step_started_ms < hold_ms) {
        return Result::RUNNING;
    }

    _step = (_step == INTRO_STEP) ? 0 : _step + 1;

    if (_step < TONE_COUNT) {
        drawToneRow(screen, _step, COLOR_YELLOW, "playing");
        // Blocks for TONE_MS while samples stream to I2S.
        bool played = _context.tone->playTone(TONES_HZ[_step], TONE_MS, TONE_VOLUME);
        _all_ok = _all_ok && played;
        drawToneRow(screen, _step, played ? COLOR_GREEN : COLOR_RED, played ? "OK" : "error");
        _step_started_ms = millis();
        return Result::RUNNING;
    }

    screen.setField(MP3_STEP + 2, "MP3 ding.mp3", COLOR_YELLOW, "playing");
    _mp3_queued = _context.mp3 && _context.mp3->playFile(MP3_PATH);
    _step_started_ms = millis();
    return Result::RUNNING;
}

void SoundTest::drawToneRow(TestScreen& screen, uint8_t index, uint16_t color,
                            const char* state) {
    char label[16];
    snprintf(label, sizeof(label), "Tone %u Hz", TONES_HZ[index]);
    screen.setField(index + 2, label, color, "%s", state);
}

void SoundTest::finish() {
    if (_context.mp3) {
        _context.mp3->stop();
    }
}
