/**
 * Axp2101.h
 *
 * Declarations for Axp2101.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <XPowersLib.h>
#include <functional>

// Forward declarations
/**
 * @brief SharedI2cBus.
 */
class SharedI2cBus;

// Type definitions
typedef std::function<void()> ButtonCallback;

/**
 * Snapshot of PMIC power readings, filled by AXP2101::readTelemetry().
 *
 * The AXP2101 has no current-sense ADC: charge and discharge current cannot be
 * measured, only voltages, fuel-gauge percent, and charger/discharge state.
 */
struct PowerTelemetry {
    bool battery_connected;
    uint8_t battery_percent;
    uint16_t battery_mv;
    bool vbus_in;
    uint16_t vbus_mv;
    uint16_t system_mv;
    bool dc1_enabled;
    uint16_t dc1_mv;
    bool charging;
    bool discharging;         // battery is supplying the system
    uint8_t charger_state;    // xpowers_chg_status_t
    uint16_t charge_limit_ma; // configured constant-current limit, not measured
    float die_temp_c;
};

/**
 * AXP2101 - Power management controller
 *
 * Features:
 * - Battery charging management
 * - Power button monitoring
 * - Button event callbacks
 *
 * Hardware: AXP2101 PMIC via I2C
 */
class AXP2101 {
public:
    /**
     * @brief Construct AXP2101 power manager instance
     *
     * @param i2c_bus Pointer to shared I2C bus instance
     * @param i2c_addr I2C device address
     */
    AXP2101(SharedI2cBus* i2c_bus, uint8_t i2c_addr);

    /**
     * @brief Initialize and start the component
     *
     * @return true on success, false on failure
     */
    bool begin();

    /**
     * @brief Check if power manager is initialized
     *
     * @return true if ready, false otherwise
     */
    bool isReady() const { return _initialized; }

    /**
     * @brief Update button state (call this regularly in main loop)
     */
    void updateButton();

    /**
     * @brief Register callback for button click events
     *
     * @param callback Function to call when button is clicked
     */
    void onButtonClick(ButtonCallback callback);

    /**
     * @brief Register callback for button long press events
     *
     * @param callback Function to call when button is long-pressed
     */
    void onButtonLongPress(ButtonCallback callback);

    /**
     * @brief Enable or disable the PMIC's own power-off on a long key hold
     *
     * When disabled, holding the key never cuts power in hardware; firmware
     * must offer its own shutdown (see shutdown()). Enabled is the PMIC default.
     *
     * @return true if the PMIC accepted the setting
     */
    bool setHardwarePowerOffEnabled(bool enabled);

    /**
     * @brief true from the long-press IRQ (~1 s into a hold) until release
     */
    bool isLongPressActive() const { return _long_press_active; }

    /**
     * @brief Request PMIC shutdown
     */
    void shutdown();

    /**
     * @brief Set button debounce time
     *
     * @param ms Debounce time in milliseconds
     */
    void setDebounceTime(unsigned long ms);

    /**
     * @brief Get battery percentage
     * 
     * @param percentage Pointer to store percentage (0-100)
     * @return true if battery connected, false otherwise
     */
    bool getBatteryPercentage(uint8_t* percentage);

    /**
     * @brief Check if VBUS (USB power) is present
     */
    bool isVbusIn();

    /**
     * @brief Check if the battery is currently charging
     */
    bool isCharging();

    /**
     * @brief Enable the PMIC ADC channels that readTelemetry() samples
     *
     * Call early so the first reading has settled values.
     */
    void enableTelemetry();

    /**
     * @brief Read battery, VBUS, rail, charger and die-temperature values
     *
     * Enables the PMIC ADC channels on first use if enableTelemetry() was
     * not called.
     *
     * @param out Destination for the readings
     * @return true if the PMIC is initialized and out was filled
     */
    bool readTelemetry(PowerTelemetry* out);

    /**
     * @brief Human-readable name for a PowerTelemetry::charger_state value
     */
    static const char* chargerStateName(uint8_t state);

    /**
     * @brief Clear pending IRQ status flags
     */
    void clearIrqStatus();

    /**
     * @brief Read current IRQ status flags
     * @return Bitmask of IRQ status flags (0 if not initialized)
     */
    uint64_t getIrqStatus();

private:
    /**
     * @brief Configure battery charging parameters
     */
    void setupCharging();

    /**
     * @brief Process pending button events
     */
    void processButtonEvents();

    // Hardware interface
    XPowersAXP2101 _axp;
    SharedI2cBus* _i2c_bus;
    uint8_t _addr;
    bool _initialized;
    bool _telemetry_enabled;

    // Button state
    unsigned long _debounce_time;
    unsigned long _last_debounce_time;
    bool _pending_click;
    bool _long_press_active;
    bool _ignore_next_release;
    ButtonCallback _on_click;
    ButtonCallback _on_long_press;
};
