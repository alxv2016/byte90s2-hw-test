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
}

ClockRtc::ClockRtc()
    : _rtc()
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

    _rtc.start();
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
