/**
 * PowerTest.cpp
 *
 * Implementation for PowerTest.
 */

#include "hwtest/tests/PowerTest.h"

#include "Axp2101.h"
#include "DisplayColors.h"
#include "SharedI2cBus.h"

#include <Wire.h>

namespace {
constexpr uint32_t INTRO_MS = 800;

struct I2cDevice {
    uint8_t address;
    const char* name;
};

constexpr I2cDevice EXPECTED_DEVICES[] = {
    {0x34, "0x34 AXP2101"},
    {0x51, "0x51 PCF8563"},
    {0x53, "0x53 ADXL345"},
    {0x5A, "0x5A DRV2605"},
};
constexpr uint8_t EXPECTED_COUNT = sizeof(EXPECTED_DEVICES) / sizeof(EXPECTED_DEVICES[0]);

// DCDC1 feeds the whole board; outside this window something is wrong.
constexpr uint16_t DC1_MIN_MV = 3000;
constexpr uint16_t DC1_MAX_MV = 3600;

bool probe(TwoWire* bus, uint8_t address) {
    bus->beginTransmission(address);
    return bus->endTransmission() == 0;
}
}  // namespace

PowerTest::PowerTest(HwTestContext& context)
    : HardwareTest(context)
    , _started_ms(0)
    , _done(false) {
}

void PowerTest::start(TestScreen& screen) {
    _started_ms = millis();
    _done = false;
    screen.setLine(0, COLOR_WHITE, "Scanning I2C bus...");
}

HardwareTest::Result PowerTest::update(TestScreen& screen) {
    if (_done || millis() - _started_ms < INTRO_MS) {
        return Result::RUNNING;
    }
    _done = true;

    SharedI2cBus& i2c = SharedI2cBus::getInstance();
    TwoWire* bus = i2c.isReady() ? i2c.getBus() : nullptr;
    if (!bus) {
        screen.setLine(0, COLOR_RED, "I2C bus not ready");
        return Result::FAILED;
    }

    bool passed = true;
    for (uint8_t i = 0; i < EXPECTED_COUNT; i++) {
        bool found = probe(bus, EXPECTED_DEVICES[i].address);
        passed = passed && found;
        screen.setField(i, EXPECTED_DEVICES[i].name, found ? COLOR_GREEN : COLOR_RED, "%s",
                        found ? "OK" : "missing");
    }

    PowerTelemetry telemetry = {};
    if (!_context.power || !_context.power->readTelemetry(&telemetry)) {
        screen.setLine(5, COLOR_RED, "PMIC init failed");
        return Result::FAILED;
    }

    bool dc1_ok = telemetry.dc1_enabled &&
                  telemetry.dc1_mv >= DC1_MIN_MV && telemetry.dc1_mv <= DC1_MAX_MV;
    passed = passed && dc1_ok;

    screen.setField(4, "DC1 rail", dc1_ok ? COLOR_GREEN : COLOR_RED, "%s %u mV",
                    telemetry.dc1_enabled ? "on" : "OFF", telemetry.dc1_mv);
    screen.setField(5, "System", COLOR_WHITE, "%u mV", telemetry.system_mv);
    screen.setField(6, "PMIC temp", COLOR_WHITE, "%.0f C", telemetry.die_temp_c);
    if (telemetry.vbus_in) {
        screen.setField(7, "USB", COLOR_WHITE, "in %u mV", telemetry.vbus_mv);
    } else {
        screen.setField(7, "USB", COLOR_WHITE, "out");
    }
    screen.setField(8, "Charger", COLOR_WHITE, "%s",
                    AXP2101::chargerStateName(telemetry.charger_state));

    return passed ? Result::PASSED : Result::FAILED;
}
