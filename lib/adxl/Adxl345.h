/**
 * @file Adxl345.h
 * @brief ADXL345 accelerometer wrapper
 *
 * Provides initialization and data access helpers
 * for the ADXL345 accelerometer.
 */

#pragma once

#include <Adafruit_Sensor.h>
#include <Arduino.h>
#include <Wire.h>

#define ADXL_FORCE_SCALE_FACTOR 62.5f
#define ADXL_DURATION_SCALE_FACTOR 0.625f
#define ADXL_LATENCY_SCALE_FACTOR 1.25f

#define ADXL345_DEFAULT_ADDRESS (0x53)
#define ADXL345_REG_DEVID (0x00)
#define ADXL345_REG_THRESH_TAP (0x1D)
#define ADXL345_REG_DUR (0x21)
#define ADXL345_REG_LATENT (0x22)
#define ADXL345_REG_WINDOW (0x23)
#define ADXL345_REG_ACT_TAP_STATUS (0x2B)
#define ADXL345_REG_TAP_AXES (0x2A)
#define ADXL345_REG_BW_RATE (0x2C)
#define ADXL345_REG_POWER_CTL (0x2D)
#define ADXL345_REG_INT_ENABLE (0x2E)
#define ADXL345_REG_INT_MAP (0x2F)
#define ADXL345_REG_INT_SOURCE (0x30)
#define ADXL345_REG_DATA_FORMAT (0x31)
#define ADXL345_REG_DATAX0 (0x32)
#define ADXL345_REG_DATAY0 (0x34)
#define ADXL345_REG_DATAZ0 (0x36)
#define ADXL345_REG_FIFO_CTL (0x38)
#define ADXL345_REG_FIFO_STATUS (0x39)

#define ADXL345_MG2G_MULTIPLIER (0.004f)

enum adxl345_data_rate_t {
    ADXL345_DATARATE_3200_HZ = 0b1111,
    ADXL345_DATARATE_1600_HZ = 0b1110,
    ADXL345_DATARATE_800_HZ = 0b1101,
    ADXL345_DATARATE_400_HZ = 0b1100,
    ADXL345_DATARATE_200_HZ = 0b1011,
    ADXL345_DATARATE_100_HZ = 0b1010,
    ADXL345_DATARATE_50_HZ = 0b1001,
    ADXL345_DATARATE_25_HZ = 0b1000,
    ADXL345_DATARATE_12_5_HZ = 0b0111,
    ADXL345_DATARATE_6_25HZ = 0b0110,
    ADXL345_DATARATE_3_13_HZ = 0b0101,
    ADXL345_DATARATE_1_56_HZ = 0b0100,
    ADXL345_DATARATE_0_78_HZ = 0b0011,
    ADXL345_DATARATE_0_39_HZ = 0b0010,
    ADXL345_DATARATE_0_20_HZ = 0b0001,
    ADXL345_DATARATE_0_10_HZ = 0b0000
};

enum adxl345_range_t {
    ADXL345_RANGE_2_G = 0b00,
    ADXL345_RANGE_4_G = 0b01,
    ADXL345_RANGE_8_G = 0b10,
    ADXL345_RANGE_16_G = 0b11
};

class SharedI2cBus;

/**
 * @brief Adxl345.
 */
class Adxl345 {
public:
    /**
     * @brief Construct ADXL345 wrapper
     */
    Adxl345();

    /**
     * @brief Initialize and configure the ADXL345
     *
     * @param i2c_bus Pointer to initialized shared I2C bus
     * @return true on success, false otherwise
     */
    bool begin(SharedI2cBus* i2c_bus);

    /**
     * @brief Check if sensor is initialized
     *
     * @return true if ready, false otherwise
     */
    bool isReady() const { return _enabled; }

    /**
     * @brief Read latest sensor event
     *
     * @param event Output event data
     * @return true if event read, false otherwise
     */
    bool getEvent(sensors_event_t* event);

    /**
     * @brief Read a register from the ADXL345
     *
     * @param reg Register address
     * @return Register value
     */
    uint8_t readRegister(uint8_t reg);

    /**
     * @brief Clear pending interrupts
     */
    void clearInterrupts();

    /**
     * @brief Calculate smoothed acceleration magnitude
     *
     * @param accel_x X-axis acceleration in m/s^2
     * @param accel_y Y-axis acceleration in m/s^2
     * @param accel_z Z-axis acceleration in m/s^2
     * @return Smoothed magnitude as integer
     */
    int calculateCombinedMagnitude(float accel_x, float accel_y, float accel_z);

    /**
     * @brief Read FIFO sample count
     *
     * @return Number of samples in FIFO, or 0 if not ready
     */
    uint8_t getFifoSampleCount();

private:
    bool retryInit(SharedI2cBus* i2c_bus, uint8_t attempts);
    bool writeRegisterInternal(uint8_t reg, uint8_t value);
    bool readRegisterInternal(uint8_t reg, uint8_t* value);
    bool readRegister16(uint8_t reg, int16_t* value);
    bool setRange(adxl345_range_t range);
    bool setDataRate(adxl345_data_rate_t data_rate);
    uint8_t calcGforce(float gforce);
    uint8_t calcDuration(float duration_ms);
    uint8_t calcLatency(float latency_ms);

    TwoWire* _bus;
    uint8_t _address;
    int32_t _sensor_id;
    bool _enabled;
};
