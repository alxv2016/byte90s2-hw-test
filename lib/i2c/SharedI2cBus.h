/**
 * @file SharedI2cBus.h
 * @brief Centralized I2C bus management for BYTE-90
 *
 * Manages the shared I2C bus used by multiple modules to prevent conflicts
 * and duplicate initialization.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

/**
 * SharedI2cBus - Singleton class for managing shared I2C bus
 *
 * Features:
 * - Single initialization point for I2C bus
 * - Prevents duplicate initialization
 * - Provides I2C bus instance to multiple modules
 * - Device scanning and diagnostics
 * - Bus reset capability
 *
 * Usage:
 *   SharedI2cBus::getInstance().begin(SDA_PIN, SCL_PIN);
 *   TwoWire* bus = SharedI2cBus::getInstance().getBus();
 */
class SharedI2cBus {
public:
    /**
     * @brief Get the singleton instance
     * @return Reference to the SharedI2cBus instance
     */
    static SharedI2cBus& getInstance();

    /**
     * @brief Initialize the I2C bus
     * @param sda_pin I2C SDA pin number
     * @param scl_pin I2C SCL pin number
     * @param frequency I2C clock frequency (default: 400kHz)
     * @return true if initialization successful, false otherwise
     */
    bool begin(int8_t sda_pin, int8_t scl_pin, uint32_t frequency = 400000);

    /**
     * @brief Check if I2C bus is initialized and ready
     * @return true if ready, false otherwise
     */
    bool isReady() const { return _initialized; }

    /**
     * @brief Get the I2C bus instance
     * @return Pointer to the Wire instance, or nullptr if not initialized
     */
    TwoWire* getBus();

    /**
     * @brief Scan I2C bus for connected devices
     * @return Number of devices found
     */
    uint8_t scanDevices();

    /**
     * @brief Reset I2C bus in case of communication errors
     * @return true if reset successful, false otherwise
     */
    bool reset();

    /**
     * @brief Get SDA pin number
     * @return SDA pin, or -1 if not initialized
     */
    int8_t getSDAPin() const { return _sda_pin; }

    /**
     * @brief Get SCL pin number
     * @return SCL pin, or -1 if not initialized
     */
    int8_t getSCLPin() const { return _scl_pin; }

    /**
     * @brief Get I2C frequency
     * @return Frequency in Hz, or 0 if not initialized
     */
    uint32_t getFrequency() const { return _frequency; }

private:
    // Singleton pattern - private constructor
    SharedI2cBus();
    ~SharedI2cBus() = default;

    // Prevent copying
    SharedI2cBus(const SharedI2cBus&) = delete;
    SharedI2cBus& operator=(const SharedI2cBus&) = delete;

    bool _initialized;
    int8_t _sda_pin;
    int8_t _scl_pin;
    uint32_t _frequency;
};
