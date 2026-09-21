/**
 * BatteryInfoTest.cpp
 *
 * Implementation for BatteryInfoTest.
 */

#include "hwtest/tests/BatteryInfoTest.h"

#include "Axp2101.h"
#include "DisplayColors.h"

#include <esp_log.h>

namespace {
static const char* TAG = "BatteryTest";
constexpr uint32_t SAMPLE_DURATION_MS = 6000;
constexpr uint32_t DRAW_INTERVAL_MS = 1000;
constexpr uint32_t TOGGLE_TIMEOUT_MS = 30000;
// Let the PMIC switch power paths and the ADC settle before reading.
constexpr uint32_t SETTLE_MS = 1500;

// Plausible single-cell Li-ion window; outside it the gauge or cell is suspect.
constexpr uint16_t CELL_MIN_MV = 3000;
constexpr uint16_t CELL_MAX_MV = 4350;

// Row layout; 0-4 refresh live, 5-6 hold USB toggle results.
constexpr uint8_t ROW_LEVEL = 0;
constexpr uint8_t ROW_BAR = 1;
constexpr uint8_t ROW_USB = 2;
constexpr uint8_t ROW_STATE = 3;
constexpr uint8_t ROW_TREND = 4;
constexpr uint8_t ROW_FIRST_TOGGLE = 5;
constexpr uint8_t ROW_SECOND_TOGGLE = 6;
constexpr uint8_t ROW_PROMPT = 7;
constexpr uint8_t ROW_RESULT = 8;

uint16_t percentColor(uint8_t percent) {
    if (percent >= 50) {
        return COLOR_GREEN;
    }
    return percent >= 20 ? COLOR_YELLOW : COLOR_RED;
}

// Value for the "Charge" field. "<=400mA" is the configured limit; the
// AXP2101 cannot measure the actual current.
void formatChargeState(const PowerTelemetry& telemetry, char* out, size_t out_size) {
    if (telemetry.charging) {
        snprintf(out, out_size, "%s <=%umA", AXP2101::chargerStateName(telemetry.charger_state),
                 telemetry.charge_limit_ma);
    } else if (telemetry.discharging) {
        snprintf(out, out_size, "no, on batt");
    } else if (telemetry.charger_state == XPOWERS_AXP2101_CHG_DONE_STATE) {
        snprintf(out, out_size, "done (full)");
    } else {
        snprintf(out, out_size, "idle");
    }
}
}  // namespace

BatteryInfoTest::BatteryInfoTest(HwTestContext& context)
    : HardwareTest(context)
    , _phase(Phase::SAMPLING)
    , _phase_started_ms(0)
    , _last_draw_ms(0)
    , _usb_at_start(false)
    , _first_mv(0)
    , _min_mv(UINT16_MAX)
    , _max_mv(0)
    , _last_mv_on_usb(0) {
}

void BatteryInfoTest::start(TestScreen& screen) {
    _phase = Phase::SAMPLING;
    _phase_started_ms = millis();
    _last_draw_ms = 0;
    _first_mv = 0;
    _min_mv = UINT16_MAX;
    _max_mv = 0;
    _last_mv_on_usb = 0;

    PowerTelemetry telemetry = {};
    _usb_at_start = _context.power && _context.power->readTelemetry(&telemetry) &&
                    telemetry.vbus_in;
}

HardwareTest::Result BatteryInfoTest::update(TestScreen& screen) {
    PowerTelemetry telemetry = {};
    if (!_context.power || !_context.power->readTelemetry(&telemetry)) {
        screen.setLine(ROW_RESULT, COLOR_RED, "PMIC not ready");
        return Result::FAILED;
    }
    if (!telemetry.battery_connected) {
        screen.setField(ROW_LEVEL, "Battery", COLOR_RED, "not detected");
        screen.setField(ROW_USB, "USB", COLOR_WHITE, "%s %u mV", telemetry.vbus_in ? "in" : "out",
                        telemetry.vbus_mv);
        screen.setLine(ROW_RESULT, COLOR_RED, "Skipped USB unplug");
        return Result::FAILED;
    }

    uint32_t now = millis();
    uint32_t elapsed_ms = now - _phase_started_ms;

    if (_first_mv == 0) {
        _first_mv = telemetry.battery_mv;
    }
    _min_mv = min(_min_mv, telemetry.battery_mv);
    _max_mv = max(_max_mv, telemetry.battery_mv);
    if (telemetry.vbus_in) {
        _last_mv_on_usb = telemetry.battery_mv;
    }

    if (_last_draw_ms == 0 || now - _last_draw_ms >= DRAW_INTERVAL_MS) {
        _last_draw_ms = now;
        drawLive(screen, telemetry, false);
    }

    switch (_phase) {
        case Phase::SAMPLING:
            if (elapsed_ms < SAMPLE_DURATION_MS) {
                screen.updateLine(ROW_PROMPT, COLOR_YELLOW, "> Sampling %lu s",
                                  static_cast<unsigned long>((SAMPLE_DURATION_MS - elapsed_ms) / 1000 + 1));
                return Result::RUNNING;
            }
            drawLive(screen, telemetry, true);
            ESP_LOGI(TAG, "Sampling done; waiting for USB %s (%lu s)",
                     _usb_at_start ? "unplug" : "plug-in",
                     static_cast<unsigned long>(TOGGLE_TIMEOUT_MS / 1000));
            _phase = Phase::WAIT_FIRST_TOGGLE;
            _phase_started_ms = now;
            return Result::RUNNING;

        case Phase::WAIT_FIRST_TOGGLE:
        case Phase::WAIT_SECOND_TOGGLE: {
            bool first = _phase == Phase::WAIT_FIRST_TOGGLE;
            // First toggle leaves the starting state, second returns to it.
            bool want_usb = first ? !_usb_at_start : _usb_at_start;
            const char* action = want_usb ? "Plug in" : "Unplug";
            if (telemetry.vbus_in == want_usb) {
                ESP_LOGI(TAG, "USB %s after %lu ms; settling", want_usb ? "plugged in" : "unplugged",
                         static_cast<unsigned long>(elapsed_ms));
                _phase = first ? Phase::SETTLE_FIRST_TOGGLE : Phase::SETTLE_SECOND_TOGGLE;
                _phase_started_ms = now;
                screen.updateLine(ROW_PROMPT, COLOR_YELLOW, "> Reading...");
                return Result::RUNNING;
            }
            if (elapsed_ms >= TOGGLE_TIMEOUT_MS) {
                screen.setField(first ? ROW_FIRST_TOGGLE : ROW_SECOND_TOGGLE, action, COLOR_RED,
                                "not seen");
                screen.updateLine(ROW_PROMPT, COLOR_WHITE, "%s", "");
                screen.setLine(ROW_RESULT, COLOR_RED, "USB change missed");
                return Result::FAILED;
            }
            screen.updateLine(ROW_PROMPT, COLOR_YELLOW, "> %s USB  %2lu s", action,
                              static_cast<unsigned long>((TOGGLE_TIMEOUT_MS - elapsed_ms) / 1000));
            return Result::RUNNING;
        }

        case Phase::SETTLE_FIRST_TOGGLE:
            if (elapsed_ms < SETTLE_MS) {
                return Result::RUNNING;
            }
            reportToggle(screen, ROW_FIRST_TOGGLE, telemetry);
            ESP_LOGI(TAG, "Waiting for USB %s (%lu s)", _usb_at_start ? "plug-in" : "unplug",
                     static_cast<unsigned long>(TOGGLE_TIMEOUT_MS / 1000));
            _phase = Phase::WAIT_SECOND_TOGGLE;
            _phase_started_ms = now;
            return Result::RUNNING;

        case Phase::SETTLE_SECOND_TOGGLE:
            if (elapsed_ms < SETTLE_MS) {
                return Result::RUNNING;
            }
            reportToggle(screen, ROW_SECOND_TOGGLE, telemetry);
            return finish(screen);
    }
    return Result::RUNNING;
}

void BatteryInfoTest::drawLive(TestScreen& screen, const PowerTelemetry& telemetry, bool log) {
    char value[24];
    auto put = [&](uint8_t row, const char* label, uint16_t color) {
        if (log) {
            screen.setField(row, label, color, "%s", value);
        } else {
            screen.updateField(row, label, color, "%s", value);
        }
    };

    uint16_t level_color = percentColor(telemetry.battery_percent);
    snprintf(value, sizeof(value), "%u%%  %u mV", telemetry.battery_percent,
             telemetry.battery_mv);
    put(ROW_LEVEL, "Battery", level_color);
    screen.drawBar(ROW_BAR, telemetry.battery_percent, level_color);

    if (telemetry.vbus_in) {
        snprintf(value, sizeof(value), "in %u mV", telemetry.vbus_mv);
    } else {
        snprintf(value, sizeof(value), "out");
    }
    put(ROW_USB, "USB", COLOR_WHITE);

    formatChargeState(telemetry, value, sizeof(value));
    put(ROW_STATE, "Charge", telemetry.discharging ? COLOR_YELLOW : COLOR_WHITE);

    if (_phase == Phase::SAMPLING) {
        int32_t delta_mv = static_cast<int32_t>(telemetry.battery_mv) - _first_mv;
        snprintf(value, sizeof(value), "%+ld mV / %lu s", static_cast<long>(delta_mv),
                 static_cast<unsigned long>((millis() - _phase_started_ms) / 1000));
        put(ROW_TREND, "Trend", COLOR_WHITE);
    }
}

void BatteryInfoTest::reportToggle(TestScreen& screen, uint8_t row,
                                   const PowerTelemetry& telemetry) {
    drawLive(screen, telemetry, true);

    if (!telemetry.vbus_in) {
        // Now on battery: the drop from the last USB reading is the load sag.
        int32_t sag_mv = _last_mv_on_usb > 0
                             ? static_cast<int32_t>(telemetry.battery_mv) - _last_mv_on_usb
                             : 0;
        screen.setField(row, "Unplug", telemetry.discharging ? COLOR_GREEN : COLOR_YELLOW,
                        "OK, %+ld mV", static_cast<long>(sag_mv));
        if (!telemetry.discharging) {
            screen.setLine(ROW_TREND, COLOR_YELLOW, "PMIC not discharging");
        }
        return;
    }

    bool full = telemetry.charger_state == XPOWERS_AXP2101_CHG_DONE_STATE;
    const char* outcome = telemetry.charging ? "charging" : (full ? "full" : "no chg");
    screen.setField(row, "Plug in", telemetry.charging || full ? COLOR_GREEN : COLOR_YELLOW,
                    "OK, %s", outcome);
}

HardwareTest::Result BatteryInfoTest::finish(TestScreen& screen) {
    screen.setField(ROW_PROMPT, "Range", COLOR_WHITE, "%u-%u mV", _min_mv, _max_mv);

    bool voltage_ok = _min_mv >= CELL_MIN_MV && _max_mv <= CELL_MAX_MV;
    screen.setLine(ROW_RESULT, voltage_ok ? COLOR_GREEN : COLOR_RED, "%s",
                   voltage_ok ? "Cell + USB detect OK" : "Cell mV out of range");
    return voltage_ok ? Result::PASSED : Result::FAILED;
}
