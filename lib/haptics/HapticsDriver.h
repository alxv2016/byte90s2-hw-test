/**
 * @file HapticsDriver.h
 * @brief Asynchronous haptic feedback using DRV2605L haptic driver
 *
 * Provides non-blocking haptic feedback for audio events using FreeRTOS tasks
 * and integrates with the shared I2C bus.
 */

#pragma once

#include <Arduino.h>
#include <Adafruit_DRV2605.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

// Forward declarations
/**
 * @brief SharedI2cBus.
 */
class SharedI2cBus;

//==============================================================================
// HAPTIC EFFECT DEFINITIONS (DRV2605L ROM Library)
//==============================================================================

// Basic click effects
#define HAPTIC_STRONG_CLICK_100     1
#define HAPTIC_SHARP_CLICK_100      4
#define HAPTIC_SOFT_BUMP_100        7
#define HAPTIC_DOUBLE_CLICK_100     10
#define HAPTIC_TRIPLE_CLICK_100     12

// Buzz and alert effects
#define HAPTIC_STRONG_BUZZ_100      14
#define HAPTIC_ALERT_750MS          15
#define HAPTIC_ALERT_1000MS         16
#define HAPTIC_BUZZ_1_100           47
#define HAPTIC_LONG_BUZZ_100        118

// Click variations
#define HAPTIC_STRONG_CLICK_1_100   17
#define HAPTIC_MEDIUM_CLICK_1_100   21
#define HAPTIC_SHARP_TICK_1_100     24

// Double click variations
#define HAPTIC_SHORT_DOUBLE_CLICK_STRONG_1_100      27
#define HAPTIC_SHORT_DOUBLE_CLICK_MEDIUM_1_100      31
#define HAPTIC_SHORT_DOUBLE_SHARP_TICK_1_100        34
#define HAPTIC_LONG_DOUBLE_SHARP_CLICK_STRONG_1_100 37
#define HAPTIC_LONG_DOUBLE_SHARP_CLICK_MEDIUM_1_100 41
#define HAPTIC_LONG_DOUBLE_SHARP_TICK_1_100         44

// Pulsing effects
#define HAPTIC_PULSING_STRONG_1_100  52
#define HAPTIC_PULSING_MEDIUM_1_100  54
#define HAPTIC_PULSING_SHARP_1_100   56

// Transition effects
#define HAPTIC_TRANSITION_CLICK_1_100    58
#define HAPTIC_TRANSITION_HUM_1_100      64

// Ramp down effects
#define HAPTIC_RAMP_DOWN_LONG_SMOOTH_1_100      70
#define HAPTIC_RAMP_DOWN_LONG_SMOOTH_2_100      71
#define HAPTIC_RAMP_DOWN_MEDIUM_SMOOTH_1_100    72
#define HAPTIC_RAMP_DOWN_MEDIUM_SMOOTH_2_100    73
#define HAPTIC_RAMP_DOWN_SHORT_SMOOTH_1_100     74
#define HAPTIC_RAMP_DOWN_SHORT_SMOOTH_2_100     75
#define HAPTIC_RAMP_DOWN_LONG_SHARP_1_100       76
#define HAPTIC_RAMP_DOWN_LONG_SHARP_2_100       77
#define HAPTIC_RAMP_DOWN_MEDIUM_SHARP_1_100     78
#define HAPTIC_RAMP_DOWN_MEDIUM_SHARP_2_100     79
#define HAPTIC_RAMP_DOWN_SHORT_SHARP_1_100      80
#define HAPTIC_RAMP_DOWN_SHORT_SHARP_2_100      81

// Ramp up effects
#define HAPTIC_RAMP_UP_LONG_SMOOTH_1_100        82
#define HAPTIC_RAMP_UP_LONG_SHARP_1_100         88

/**
 * HapticsDriver - Non-blocking DRV2605 haptic output driver
 *
 * Features:
 * - Asynchronous haptic effects using FreeRTOS
 * - Predefined patterns for audio events
 * - Power management with activity timeout
 * - Thread-safe operation
 *
 * Usage:
 *   HapticsDriver* haptics = new HapticsDriver(&i2c_bus);
 *   haptics->begin();
 *   haptics->playEventHaptic(HAPTIC_EVENT_SPEAKING);
 */
class HapticsDriver {
public:
    /**
     * @brief Haptic event types for audio integration
     */
    enum HapticEvent {
        HAPTIC_EVENT_ONLINE,        // Connected to service
        HAPTIC_EVENT_DISCONNECT,    // Disconnected from service
        HAPTIC_EVENT_INTERRUPT,     // Audio interrupted
        HAPTIC_EVENT_SPEAKING,      // Device starts speaking
        HAPTIC_EVENT_BUTTON_CLICK,  // Button pressed
        HAPTIC_EVENT_ERROR          // Error occurred
    };

    /**
     * @brief Actuator type configuration
     */
    enum ActuatorType {
        ACTUATOR_ERM = 0,  // Coin cell haptic actuator
        ACTUATOR_LRA = 1   // Cylindrical haptic actuator
    };

    /**
     * @brief Construct haptics manager instance
     *
     * @param i2c_bus Pointer to shared I2C bus instance
     */
    HapticsDriver(SharedI2cBus* i2c_bus);

    /**
     * @brief Destroy haptics manager and cleanup resources
     */
    ~HapticsDriver();

    /**
     * @brief Initialize haptics hardware
     *
     * @param actuator_type Type of haptic actuator (default: ERM)
     * @return true if initialization successful, false otherwise
     */
    bool begin(ActuatorType actuator_type = ACTUATOR_ERM);

    /**
     * @brief Check if haptics is initialized and ready
     *
     * @return true if ready, false otherwise
     */
    bool isReady() const { return _initialized; }

    /**
     * @brief Play haptic feedback for an audio event (non-blocking)
     *
     * @param event Event type to play haptic for
     * @return true if effect started, false otherwise
     */
    bool playEventHaptic(HapticEvent event);

    /**
     * @brief Play a raw haptic effect from ROM library (non-blocking)
     *
     * @param effect_id Effect ID to play (1-123)
     * @return true if effect started, false otherwise
     */
    bool playEffect(uint8_t effect_id);

    /**
     * @brief Start asynchronous pulse vibration
     *
     * @param intensity Vibration intensity (0-255)
     * @param duration_ms Duration in milliseconds
     * @return true if pulse started, false otherwise
     */
    bool playPulse(uint8_t intensity, uint16_t duration_ms);

    /**
     * @brief Stop any currently playing haptic effect
     */
    void stop();

    /**
     * @brief Enable haptics
     */
    void enable();

    /**
     * @brief Disable haptics for power saving
     */
    void disable();

    /**
     * @brief Check if haptics are currently enabled
     *
     * @return true if enabled, false if disabled
     */
    bool isEnabled() const { return _enabled; }

private:
    /**
     * @brief FreeRTOS timer callback for pulse vibration
     */
    static void pulseTimerCallback(TimerHandle_t timer);

    /**
     * @brief Stop pulse vibration (called by timer)
     */
    void stopPulse();

    // Hardware
    SharedI2cBus* _i2c_bus;
    Adafruit_DRV2605 _drv;
    ActuatorType _actuator_type;
    bool _initialized;
    bool _enabled;

    // Async pulse support
    TimerHandle_t _pulse_timer;
    bool _pulse_active;
};
