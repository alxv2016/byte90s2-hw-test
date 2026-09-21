/**
 * ClockRtc.h
 *
 * Declarations for ClockRtc.
 */

#pragma once

#include <Arduino.h>
#include <RTClib.h>

class SharedI2cBus;

/**
 * ClockRtcState - RTC availability and time validity.
 */
enum class ClockRtcState {
    UNINITIALIZED = 0,
    READY,
    TIME_INVALID,
    ERROR
};

/**
 * ClockTime - RTC time snapshot.
 */
struct ClockTime {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t day_of_week = 0;
    uint32_t unix_time = 0;
};

/**
 * ClockRtc - PCF8563 RTC hardware interface.
 *
 * Responsibilities:
 * - Initialize and monitor the PCF8563 RTC over I2C.
 * - Read and write RTC time.
 * - Track lost-power state without auto-setting build time.
 * - Force Control_status_1 to normal mode on begin (TEST1, STOP, TESTC = 0).
 */
class ClockRtc {
public:
    ClockRtc();

    bool begin(SharedI2cBus& i2c);
    void maintenance();

    ClockRtcState getState() const;
    bool isReady() const;
    bool hasLostPower();

    bool readTime(ClockTime* out_time);
    bool setTime(const ClockTime& time);
    bool setEpoch(time_t epoch_seconds);

    bool syncFromSystemTime();

    // Logs raw control and time registers, decoded, for oscillator debugging.
    void logRegisters(const char* label);

private:
    bool readRegisters(uint8_t start_reg, uint8_t* out, size_t len);
    bool writeRegister(uint8_t reg, uint8_t value);
    ClockRtcState updateState();
    bool convertFromDateTime(const DateTime& dt, ClockTime* out_time);
    DateTime convertToDateTime(const ClockTime& time);

    RTC_PCF8563 _rtc;
    TwoWire* _bus;
    ClockRtcState _state;
};
