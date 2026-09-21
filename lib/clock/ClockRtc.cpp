/**
 * ClockRtc.cpp
 *
 * Implementation for ClockRtc.
 */

#include "ClockRtc.h"

#include "SharedI2cBus.h"

#include <esp_log.h>
#include <time.h>

namespace {
static const char* log_tag = "ClockRtc";
static const time_t min_valid_time = 1600000000;

constexpr uint8_t PCF8563_ADDRESS = 0x51;
constexpr uint8_t REG_CONTROL_1 = 0x00;
constexpr uint8_t REG_CLKOUT_CONTROL = 0x0D;
// Control_status_1 bits; all three must be 0 for the RTC to count from the
// crystal (datasheet table 5).
constexpr uint8_t CONTROL_1_TEST1 = 1 << 7;
constexpr uint8_t CONTROL_1_STOP = 1 << 5;
constexpr uint8_t CONTROL_1_TESTC = 1 << 3;
// Control_status_1, Control_status_2, VL_seconds through Years.
constexpr size_t DUMP_LEN = 9;
}

ClockRtc::ClockRtc()
    : _rtc()
    , _bus(nullptr)
    , _state(ClockRtcState::UNINITIALIZED) {}

bool ClockRtc::begin(SharedI2cBus& i2c) {
    if (!i2c.isReady()) {
        ESP_LOGE(log_tag, "I2C bus not initialized");
        _state = ClockRtcState::ERROR;
        return false;
    }

    TwoWire* bus = i2c.getBus();
    if (bus == nullptr) {
        ESP_LOGE(log_tag, "I2C bus unavailable");
        _state = ClockRtcState::ERROR;
        return false;
    }

    if (!_rtc.begin(bus)) {
        ESP_LOGE(log_tag, "PCF8563 not detected");
        _state = ClockRtcState::ERROR;
        return false;
    }

    _bus = bus;
    logRegisters("before init");

    // RTClib's start() clears only STOP. TEST1 (EXT_CLK test mode) or an active
    // POR override also freeze the clock while I2C keeps working, and only
    // writing TESTC = 0 exits the override, so reset the whole register.
    if (!writeRegister(REG_CONTROL_1, 0x00)) {
        ESP_LOGE(log_tag, "Control_status_1 write failed");
    }
    logRegisters("after init");

    _state = updateState();
    return _state != ClockRtcState::ERROR;
}

void ClockRtc::maintenance() {
    if (_state != ClockRtcState::UNINITIALIZED) {
        _state = updateState();
    }
}

ClockRtcState ClockRtc::getState() const {
    return _state;
}

bool ClockRtc::isReady() const {
    return _state == ClockRtcState::READY || _state == ClockRtcState::TIME_INVALID;
}

bool ClockRtc::hasLostPower() {
    if (_state == ClockRtcState::UNINITIALIZED || _state == ClockRtcState::ERROR) {
        return true;
    }
    return _rtc.lostPower();
}

bool ClockRtc::readTime(ClockTime* out_time) {
    if (out_time == nullptr || _state == ClockRtcState::UNINITIALIZED ||
        _state == ClockRtcState::ERROR) {
        return false;
    }

    DateTime now = _rtc.now();
    return convertFromDateTime(now, out_time);
}

bool ClockRtc::setTime(const ClockTime& time) {
    if (_state == ClockRtcState::UNINITIALIZED || _state == ClockRtcState::ERROR) {
        return false;
    }

    DateTime dt = convertToDateTime(time);
    _rtc.adjust(dt);
    _rtc.start();
    _state = updateState();
    return _state != ClockRtcState::ERROR;
}

bool ClockRtc::setEpoch(time_t epoch_seconds) {
    if (epoch_seconds < min_valid_time) {
        ESP_LOGW(log_tag, "Epoch too small, skipping RTC set");
        return false;
    }

    DateTime dt(epoch_seconds);
    _rtc.adjust(dt);
    _rtc.start();
    _state = updateState();
    return _state != ClockRtcState::ERROR;
}

bool ClockRtc::syncFromSystemTime() {
    time_t now = time(nullptr);
    return setEpoch(now);
}

void ClockRtc::logRegisters(const char* label) {
    uint8_t regs[DUMP_LEN] = {};
    uint8_t clkout = 0;
    if (!readRegisters(REG_CONTROL_1, regs, DUMP_LEN) ||
        !readRegisters(REG_CLKOUT_CONTROL, &clkout, 1)) {
        ESP_LOGE(log_tag, "[%s] register read failed", label);
        return;
    }

    uint8_t ctrl1 = regs[0];
    ESP_LOGI(log_tag, "[%s] CTRL1=0x%02X (TEST1=%d STOP=%d TESTC=%d) CTRL2=0x%02X CLKOUT=0x%02X",
             label, ctrl1, (ctrl1 & CONTROL_1_TEST1) ? 1 : 0, (ctrl1 & CONTROL_1_STOP) ? 1 : 0,
             (ctrl1 & CONTROL_1_TESTC) ? 1 : 0, regs[1], clkout);
    // Raw BCD: 20YY-MM-DD hh:mm:ss, with VL from bit 7 of VL_seconds.
    ESP_LOGI(log_tag, "[%s] VL=%d time=20%02X-%02X-%02X %02X:%02X:%02X", label,
             (regs[2] & 0x80) ? 1 : 0, regs[8], regs[7] & 0x1F, regs[5] & 0x3F, regs[4] & 0x3F,
             regs[3] & 0x7F, regs[2] & 0x7F);
}

bool ClockRtc::readRegisters(uint8_t start_reg, uint8_t* out, size_t len) {
    if (_bus == nullptr || out == nullptr) {
        return false;
    }

    _bus->beginTransmission(PCF8563_ADDRESS);
    _bus->write(start_reg);
    if (_bus->endTransmission(false) != 0) {
        return false;
    }
    if (_bus->requestFrom(PCF8563_ADDRESS, len) != len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        out[i] = _bus->read();
    }
    return true;
}

bool ClockRtc::writeRegister(uint8_t reg, uint8_t value) {
    if (_bus == nullptr) {
        return false;
    }

    _bus->beginTransmission(PCF8563_ADDRESS);
    _bus->write(reg);
    _bus->write(value);
    return _bus->endTransmission() == 0;
}

ClockRtcState ClockRtc::updateState() {
    if (!_rtc.isrunning()) {
        return ClockRtcState::ERROR;
    }

    if (_rtc.lostPower()) {
        return ClockRtcState::TIME_INVALID;
    }

    return ClockRtcState::READY;
}

bool ClockRtc::convertFromDateTime(const DateTime& dt, ClockTime* out_time) {
    if (out_time == nullptr) {
        return false;
    }

    out_time->year = dt.year();
    out_time->month = dt.month();
    out_time->day = dt.day();
    out_time->hour = dt.hour();
    out_time->minute = dt.minute();
    out_time->second = dt.second();
    out_time->day_of_week = dt.dayOfTheWeek();
    out_time->unix_time = dt.unixtime();
    return true;
}

DateTime ClockRtc::convertToDateTime(const ClockTime& time) {
    return DateTime(
        time.year,
        time.month,
        time.day,
        time.hour,
        time.minute,
        time.second
    );
}
