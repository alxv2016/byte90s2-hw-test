/**
 * ImuTest.h
 *
 * Streams ADXL345 X/Y/Z, checks that resting magnitude is close to 1 g, and
 * waits for the operator to both tap and tilt the device.
 */

#pragma once

#include "hwtest/HardwareTest.h"

class ImuTest : public HardwareTest {
public:
    explicit ImuTest(HwTestContext& context);

    const char* getName() const override { return "IMU"; }
    void start(TestScreen& screen) override;
    Result update(TestScreen& screen) override;

private:
    uint32_t _started_ms;
    uint32_t _last_sample_ms;
    float _min[3];
    float _max[3];
    bool _gravity_ok;
    bool _tapped;
    bool _tilted;
    uint16_t _read_errors;
};
