/**
 * StartupGifTest.cpp
 *
 * Implementation for StartupGifTest.
 */

#include "hwtest/tests/StartupGifTest.h"

#include "DisplayColors.h"
#include "GifPlayer.h"
#include "LittleFsAdapter.h"
#include "Mp3Player.h"

namespace {
constexpr const char* STARTUP_GIF_PATH = "/gifs/state_startup.gif";
constexpr const char* STARTUP_SOUND_PATH = "/sounds/startup-95.mp3";
constexpr uint32_t INTRO_MS = 1200;
constexpr uint32_t PLAYBACK_TIMEOUT_MS = 20000;
}  // namespace

StartupGifTest::StartupGifTest(HwTestContext& context)
    : HardwareTest(context)
    , _phase(Phase::INTRO)
    , _phase_started_ms(0)
    , _request_id(0)
    , _sound_queued(false) {
}

void StartupGifTest::start(TestScreen& screen) {
    _phase = Phase::INTRO;
    _phase_started_ms = millis();
    _request_id = 0;
    _sound_queued = false;

    screen.setField(0, "GIF", COLOR_WHITE, "state_startup");
    screen.setField(1, "Sound", COLOR_WHITE, "startup-95.mp3");
    screen.setLine(3, COLOR_CYAN, "Plays once, full");
    screen.setLine(4, COLOR_CYAN, "screen, then results");
}

HardwareTest::Result StartupGifTest::update(TestScreen& screen) {
    uint32_t now = millis();

    if (_phase == Phase::INTRO) {
        if (now - _phase_started_ms < INTRO_MS) {
            return Result::RUNNING;
        }

        if (!_context.filesystem || !_context.filesystem->isMounted()) {
            return fail(screen, "LittleFS not mounted");
        }
        if (!_context.filesystem->exists(STARTUP_GIF_PATH)) {
            return fail(screen, "GIF file missing");
        }
        if (!_context.gif || !_context.gif->isInitialized()) {
            return fail(screen, "GIF player not ready");
        }

        _request_id = _context.gif->requestGIF(STARTUP_GIF_PATH, false);
        if (_request_id == 0) {
            return fail(screen, "GIF request rejected");
        }

        _sound_queued = _context.mp3 && _context.mp3->playFile(STARTUP_SOUND_PATH);
        _phase = Phase::PLAYING;
        _phase_started_ms = now;
        return Result::RUNNING;
    }

    GifPlayer::TerminalReason reason = GifPlayer::TerminalReason::NONE;
    bool ended = _context.gif->hasRequestTerminated(_request_id, &reason);
    uint32_t elapsed_ms = now - _phase_started_ms;

    if (!ended && elapsed_ms < PLAYBACK_TIMEOUT_MS) {
        return Result::RUNNING;
    }

    if (!ended) {
        _context.gif->stopPlayback();
    }

    bool completed = ended && reason == GifPlayer::TerminalReason::COMPLETED;
    screen.redrawFrame();
    screen.setField(0, "GIF", COLOR_WHITE, "state_startup");
    screen.setField(1, "End", completed ? COLOR_GREEN : COLOR_RED, "%s",
                    ended ? GifPlayer::terminalReasonName(reason) : "timeout");
    screen.setField(2, "Time", COLOR_WHITE, "%lu ms", static_cast<unsigned long>(elapsed_ms));
    screen.setField(3, "Sound", _sound_queued ? COLOR_GREEN : COLOR_RED, "%s",
                    _sound_queued ? "played" : "failed");
    return completed ? Result::PASSED : Result::FAILED;
}

void StartupGifTest::finish() {
    // Abandoned mid-play (power menu or serial skip): stop the GIF task from
    // drawing over whatever comes next.
    bool abandoned = _phase == Phase::PLAYING && _context.gif &&
                     _context.gif->isRequestPendingOrActive(_request_id);
    if (!abandoned) {
        return;  // finished normally; let the startup sound play out
    }
    _context.gif->stopPlayback();
    if (_context.mp3) {
        _context.mp3->stop();
    }
}

HardwareTest::Result StartupGifTest::fail(TestScreen& screen, const char* reason) {
    screen.setLine(6, COLOR_RED, "%s", reason);
    return Result::FAILED;
}
