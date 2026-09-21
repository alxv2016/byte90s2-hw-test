/**
 * RtcTest.cpp
 *
 * Implementation for RtcTest.
 */

#include "hwtest/tests/RtcTest.h"

#include "ClockRtc.h"
#include "DisplayColors.h"

#include <WiFi.h>
#include <esp_log.h>

namespace {
static const char* TAG = "RtcTest";
constexpr uint32_t TICK_WINDOW_MS = 2500;
constexpr uint32_t NTP_TIMEOUT_MS = 10000;
// Any system time before 2024-01-01 means NTP has not synced yet.
constexpr time_t MIN_SYNCED_EPOCH = 1704067200;
constexpr uint32_t MAX_READBACK_DRIFT_S = 2;

// Date and time on two rows; together they are wider than one field.
void drawDateTime(TestScreen& screen, uint8_t row, const ClockTime& time) {
    screen.setField(row, "Date", COLOR_WHITE, "%04u-%02u-%02u", time.year, time.month, time.day);
    screen.setField(row + 1, "Time", COLOR_WHITE, "%02u:%02u:%02u UTC", time.hour, time.minute,
                    time.second);
}
}  // namespace

RtcTest::RtcTest(HwTestContext& context)
    : HardwareTest(context)
    , _phase(Phase::TICKING)
    , _phase_started_ms(0)
    , _first_unix(0)
    , _lost_power(false)
    , _ticking(false) {
}

void RtcTest::start(TestScreen& screen) {
    _phase = Phase::TICKING;
    _phase_started_ms = millis();
    _first_unix = 0;
    _lost_power = false;
    _ticking = false;

    screen.setField(0, "Chip", COLOR_WHITE, "PCF8563 @ 0x51");
    if (!_context.rtc || !_context.rtc->isReady()) {
        return;
    }

    _lost_power = _context.rtc->hasLostPower();
    screen.setField(1, "Power loss", _lost_power ? COLOR_YELLOW : COLOR_GREEN, "%s",
                    _lost_power ? "SET" : "clear");

    ClockTime now = {};
    if (_context.rtc->readTime(&now)) {
        drawDateTime(screen, 2, now);
        _first_unix = now.unix_time;
    }
    _context.rtc->logRegisters("test start");
    screen.setField(4, "Tick", COLOR_YELLOW, "checking...");
}

HardwareTest::Result RtcTest::update(TestScreen& screen) {
    if (!_context.rtc || !_context.rtc->isReady()) {
        screen.setLine(8, COLOR_RED, "PCF8563 not found");
        return Result::FAILED;
    }

    uint32_t elapsed_ms = millis() - _phase_started_ms;

    if (_phase == Phase::TICKING) {
        if (elapsed_ms < TICK_WINDOW_MS) {
            return Result::RUNNING;
        }
        return finishTicking(screen);
    }

    time_t ntp_now = time(nullptr);
    if (ntp_now >= MIN_SYNCED_EPOCH) {
        return finishNtp(screen, ntp_now);
    }
    if (elapsed_ms >= NTP_TIMEOUT_MS) {
        screen.setField(5, "NTP", COLOR_YELLOW, "no sync, skip");
        return _ticking ? Result::PASSED : Result::FAILED;
    }
    return Result::RUNNING;
}

HardwareTest::Result RtcTest::finishTicking(TestScreen& screen) {
    ClockTime now = {};
    if (!_context.rtc->readTime(&now)) {
        screen.setField(4, "Tick", COLOR_RED, "read failed");
        return Result::FAILED;
    }

    _context.rtc->logRegisters("tick end");
    uint32_t advanced_s = now.unix_time - _first_unix;
    ESP_LOGI(TAG, "Tick: first=%lu now=%lu", static_cast<unsigned long>(_first_unix),
             static_cast<unsigned long>(now.unix_time));
    // 2.5 s window: expect 2-3 s of RTC advance.
    _ticking = advanced_s >= 1 && advanced_s <= 4;
    screen.setField(4, "Tick", _ticking ? COLOR_GREEN : COLOR_RED, "+%lu s / 2.5 s",
                    static_cast<unsigned long>(advanced_s));

    if (WiFi.status() != WL_CONNECTED) {
        screen.setField(5, "NTP", COLOR_CYAN, "no WiFi, skip");
        if (_lost_power) {
            screen.setLine(6, COLOR_YELLOW, "Time invalid until");
            screen.setLine(7, COLOR_YELLOW, "WiFi test syncs it");
        }
        return _ticking ? Result::PASSED : Result::FAILED;
    }

    screen.setField(5, "NTP", COLOR_YELLOW, "waiting...");
    _phase = Phase::WAIT_NTP;
    _phase_started_ms = millis();
    return Result::RUNNING;
}

HardwareTest::Result RtcTest::finishNtp(TestScreen& screen, time_t ntp_now) {
    if (!_context.rtc->setEpoch(ntp_now)) {
        screen.setField(5, "NTP->RTC", COLOR_RED, "write failed");
        return Result::FAILED;
    }

    ClockTime readback = {};
    bool read_ok = _context.rtc->readTime(&readback);
    int32_t drift_s = static_cast<int32_t>(readback.unix_time) - static_cast<int32_t>(ntp_now);
    bool write_ok = read_ok && abs(drift_s) <= static_cast<int32_t>(MAX_READBACK_DRIFT_S);

    screen.setField(5, "NTP->RTC", write_ok ? COLOR_GREEN : COLOR_RED, "%s",
                    write_ok ? "written" : "mismatch");
    drawDateTime(screen, 6, readback);
    screen.setField(8, "Power loss", COLOR_WHITE, "%s",
                    _context.rtc->hasLostPower() ? "SET" : "clear");
    return (_ticking && write_ok) ? Result::PASSED : Result::FAILED;
}
