/**
 * ImuTest.cpp
 *
 * Implementation for ImuTest.
 */

#include "hwtest/tests/ImuTest.h"

#include "Adxl345.h"
#include "DisplayColors.h"

#include <esp_log.h>
#include <math.h>

namespace {
static const char* TAG = "ImuTest";
constexpr uint32_t SAMPLE_INTERVAL_MS = 150;
constexpr uint32_t TIMEOUT_MS = 20000;
constexpr uint16_t MAX_READ_ERRORS = 10;

// Resting magnitude window around 1 g (9.81 m/s^2).
constexpr float GRAVITY_MIN = 8.0f;
constexpr float GRAVITY_MAX = 11.8f;

// Axis swing that counts as a deliberate tilt.
constexpr float TILT_RANGE = 6.0f;

// ADXL345 INT_SOURCE bits for the tap detection armed in Adxl345::begin().
constexpr uint8_t INT_SINGLE_TAP = 0x40;
constexpr uint8_t INT_DOUBLE_TAP = 0x20;
}  // namespace

ImuTest::ImuTest(HwTestContext& context)
    : HardwareTest(context)
    , _started_ms(0)
    , _last_sample_ms(0)
    , _min{0, 0, 0}
    , _max{0, 0, 0}
    , _gravity_ok(false)
    , _tapped(false)
    , _tilted(false)
    , _read_errors(0) {
}

void ImuTest::start(TestScreen& screen) {
    _started_ms = millis();
    _last_sample_ms = 0;
    for (uint8_t i = 0; i < 3; i++) {
        _min[i] = INFINITY;
        _max[i] = -INFINITY;
    }
    _gravity_ok = false;
    _tapped = false;
    _tilted = false;
    _read_errors = 0;

    screen.setField(0, "Sensor", COLOR_WHITE, "ADXL345 @ 0x53");
    screen.setLine(5, COLOR_CYAN, "Tap AND tilt device");
    if (_context.imu) {
        // Drop any tap latched before the test started.
        _context.imu->clearInterrupts();
    }
}

HardwareTest::Result ImuTest::update(TestScreen& screen) {
    if (!_context.imu || !_context.imu->isReady()) {
        screen.setLine(8, COLOR_RED, "ADXL345 not found");
        return Result::FAILED;
    }

    uint32_t now = millis();
    if (now - _last_sample_ms < SAMPLE_INTERVAL_MS) {
        return Result::RUNNING;
    }
    _last_sample_ms = now;

    sensors_event_t event;
    if (!_context.imu->getEvent(&event)) {
        if (++_read_errors >= MAX_READ_ERRORS) {
            screen.setField(8, "Read errors", COLOR_RED, "%u", _read_errors);
            return Result::FAILED;
        }
        return Result::RUNNING;
    }

    static const char AXIS_NAMES[3] = {'X', 'Y', 'Z'};
    float axes[3] = {event.acceleration.x, event.acceleration.y, event.acceleration.z};
    for (uint8_t i = 0; i < 3; i++) {
        _min[i] = fminf(_min[i], axes[i]);
        _max[i] = fmaxf(_max[i], axes[i]);
        if (!_tilted && _max[i] - _min[i] >= TILT_RANGE) {
            _tilted = true;
            ESP_LOGI(TAG, "Tilt detected on %c: %.2f..%.2f m/s2", AXIS_NAMES[i], _min[i],
                     _max[i]);
        }
    }

    float magnitude = sqrtf(axes[0] * axes[0] + axes[1] * axes[1] + axes[2] * axes[2]);
    if (!_gravity_ok && magnitude >= GRAVITY_MIN && magnitude <= GRAVITY_MAX) {
        _gravity_ok = true;
        ESP_LOGI(TAG, "1g check OK: |a|=%.2f (X %.2f Y %.2f Z %.2f)", magnitude, axes[0],
                 axes[1], axes[2]);
    }

    uint8_t int_source = _context.imu->readRegister(ADXL345_REG_INT_SOURCE);
    if (!_tapped && (int_source & (INT_SINGLE_TAP | INT_DOUBLE_TAP))) {
        _tapped = true;
        ESP_LOGI(TAG, "Tap detected: INT_SOURCE=0x%02X (%s)", int_source,
                 (int_source & INT_DOUBLE_TAP) ? "double" : "single");
    }

    screen.updateField(1, "X", COLOR_WHITE, "%.2f m/s2", axes[0]);
    screen.updateField(2, "Y", COLOR_WHITE, "%.2f m/s2", axes[1]);
    screen.updateField(3, "Z", COLOR_WHITE, "%.2f m/s2", axes[2]);
    screen.updateField(4, "|a| 1g", _gravity_ok ? COLOR_GREEN : COLOR_YELLOW, "%.2f %s",
                       magnitude, _gravity_ok ? "OK" : "?");
    screen.updateField(6, "Tap", _tapped ? COLOR_GREEN : COLOR_YELLOW, "%s",
                       _tapped ? "detected" : "waiting");
    screen.updateField(7, "Tilt", _tilted ? COLOR_GREEN : COLOR_YELLOW, "%s",
                       _tilted ? "detected" : "waiting");

    bool passed = _gravity_ok && _tapped && _tilted;
    bool timed_out = now - _started_ms >= TIMEOUT_MS;
    if (passed || timed_out) {
        ESP_LOGI(TAG, "Final: X %.2f Y %.2f Z %.2f |a| %.2f m/s2", axes[0], axes[1], axes[2],
                 magnitude);
        ESP_LOGI(TAG, "Ranges: X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f", _min[0], _max[0],
                 _min[1], _max[1], _min[2], _max[2]);
        ESP_LOGI(TAG, "1g %s, tap %s, tilt %s, read errors %u", _gravity_ok ? "OK" : "no",
                 _tapped ? "yes" : "no", _tilted ? "yes" : "no", _read_errors);
    }

    if (passed) {
        screen.setLine(8, COLOR_GREEN, "1g + tap + tilt OK");
        return Result::PASSED;
    }

    if (timed_out) {
        const char* reason = "1g check failed";
        if (_gravity_ok) {
            reason = !_tapped && !_tilted ? "No tap or tilt seen"
                     : !_tapped           ? "No tap seen"
                                          : "No tilt seen";
        }
        screen.setLine(8, COLOR_RED, "%s", reason);
        return Result::FAILED;
    }
    return Result::RUNNING;
}
