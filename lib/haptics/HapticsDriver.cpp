/**
 * @file HapticsDriver.cpp
 * @brief Implementation of asynchronous haptic feedback
 */

#include "HapticsDriver.h"
#include "SharedI2cBus.h"
#include <esp_log.h>

static const char* TAG = "HAPTICS";

HapticsDriver::HapticsDriver(SharedI2cBus* i2c_bus)
    : _i2c_bus(i2c_bus)
    , _actuator_type(ACTUATOR_ERM)
    , _initialized(false)
    , _enabled(true)
    , _pulse_timer(nullptr)
    , _pulse_active(false) {
}

HapticsDriver::~HapticsDriver() {
    if (_pulse_timer) {
        xTimerDelete(_pulse_timer, 0);
        _pulse_timer = nullptr;
    }

    if (_initialized) {
        _drv.stop();
    }
}

bool HapticsDriver::begin(ActuatorType actuator_type) {
    if (_initialized) {
        ESP_LOGI(TAG, "Haptics already initialized");
        return true;
    }

    // Verify I2C bus is ready
    if (!_i2c_bus || !_i2c_bus->isReady()) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return false;
    }

    // Get shared I2C bus
    TwoWire* i2c_bus = _i2c_bus->getBus();
    if (!i2c_bus) {
        ESP_LOGE(TAG, "Failed to get I2C bus");
        return false;
    }

    // Initialize DRV2605 driver
    if (!_drv.begin(i2c_bus)) {
        ESP_LOGE(TAG, "DRV2605L not found at address 0x5A");
        return false;
    }

    _actuator_type = actuator_type;

    // Configure actuator type
    if (_actuator_type == ACTUATOR_LRA) {
        _drv.selectLibrary(6);  // LRA library
        _drv.useLRA();
        ESP_LOGI(TAG, "Configured for LRA actuator");
    } else {
        _drv.selectLibrary(1);  // ERM library
        _drv.useERM();
        ESP_LOGI(TAG, "Configured for ERM actuator");
    }

    // Set to internal trigger mode
    _drv.setMode(DRV2605_MODE_INTTRIG);

    // Create FreeRTOS timer for pulse support
    _pulse_timer = xTimerCreate(
        "haptic_pulse",
        pdMS_TO_TICKS(100),  // Initial period (will be updated)
        pdFALSE,             // One-shot timer
        this,                // Timer ID (pass this pointer)
        pulseTimerCallback   // Callback function
    );

    if (!_pulse_timer) {
        ESP_LOGE(TAG, "Failed to create pulse timer");
        return false;
    }

    _initialized = true;

    ESP_LOGI(TAG, "DRV2605L initialized successfully");
    return true;
}

bool HapticsDriver::playEventHaptic(HapticEvent event) {
    if (!_initialized || !_enabled) {
        return false;
    }

    uint8_t effect_id = 0;

    switch (event) {
        case HAPTIC_EVENT_ONLINE:
            // Double click for connection
            effect_id = HAPTIC_DOUBLE_CLICK_100;
            break;

        case HAPTIC_EVENT_DISCONNECT:
            // Pulsing for disconnection
            effect_id = HAPTIC_PULSING_STRONG_1_100;
            break;

        case HAPTIC_EVENT_INTERRUPT:
            // Strong buzz for interruption
            effect_id = HAPTIC_STRONG_BUZZ_100;
            break;

        case HAPTIC_EVENT_SPEAKING:
            // Soft bump for speaking start
            effect_id = HAPTIC_SOFT_BUMP_100;
            break;

        case HAPTIC_EVENT_BUTTON_CLICK:
            // Sharp click for button
            effect_id = HAPTIC_SHARP_CLICK_100;
            break;

        case HAPTIC_EVENT_ERROR:
            // Triple click for error
            effect_id = HAPTIC_TRIPLE_CLICK_100;
            break;

        default:
            ESP_LOGW(TAG, "Unknown haptic event: %d", event);
            return false;
    }

    return playEffect(effect_id);
}

bool HapticsDriver::playEffect(uint8_t effect_id) {
    if (!_initialized || !_enabled) {
        return false;
    }

    if (effect_id < 1 || effect_id > 123) {
        ESP_LOGE(TAG, "Invalid effect ID: %d (valid: 1-123)", effect_id);
        return false;
    }

    // Set waveform
    _drv.setWaveform(0, effect_id);
    _drv.setWaveform(1, 0);  // End of waveform

    // Trigger playback
    _drv.go();

    return true;
}

bool HapticsDriver::playPulse(uint8_t intensity, uint16_t duration_ms) {
    if (!_initialized || !_enabled) {
        return false;
    }

    // Stop any existing pulse
    if (_pulse_active) {
        stopPulse();
    }

    // Set to real-time mode for continuous vibration
    _drv.setMode(DRV2605_MODE_REALTIME);
    _drv.setRealtimeValue(intensity);

    // Start timer to stop pulse after duration
    _pulse_active = true;
    xTimerChangePeriod(_pulse_timer, pdMS_TO_TICKS(duration_ms), 0);
    xTimerStart(_pulse_timer, 0);

    ESP_LOGD(TAG, "Playing pulse: intensity=%d, duration=%dms", intensity, duration_ms);
    return true;
}

void HapticsDriver::stop() {
    if (!_initialized) {
        return;
    }

    _drv.stop();

    // If in real-time mode, return to internal trigger mode
    uint8_t mode = _drv.readRegister8(DRV2605_REG_MODE) & 0x07;
    if (mode == DRV2605_MODE_REALTIME) {
        _drv.setRealtimeValue(0);
        _drv.setMode(DRV2605_MODE_INTTRIG);
    }

    if (_pulse_active) {
        _pulse_active = false;
        xTimerStop(_pulse_timer, 0);
    }
}

void HapticsDriver::enable() {
    if (!_initialized) {
        return;
    }

    if (!_enabled) {
        _enabled = true;

        // Wake from standby if needed
        uint8_t mode = _drv.readRegister8(DRV2605_REG_MODE);
        _drv.writeRegister8(DRV2605_REG_MODE, mode & ~0x40);

        ESP_LOGI(TAG, "Haptics enabled");

        // Play feedback
        playEffect(HAPTIC_SHARP_CLICK_100);
    }
}

void HapticsDriver::disable() {
    if (!_initialized || !_enabled) {
        return;
    }

    _enabled = false;

    // Stop any playing effects
    stop();

    // Enter standby mode to save power
    uint8_t mode = _drv.readRegister8(DRV2605_REG_MODE);
    _drv.writeRegister8(DRV2605_REG_MODE, mode | 0x40);

    ESP_LOGI(TAG, "Haptics disabled");
}

void HapticsDriver::pulseTimerCallback(TimerHandle_t timer) {
    // Get HapticsDriver instance from timer ID
    HapticsDriver* instance = static_cast<HapticsDriver*>(pvTimerGetTimerID(timer));
    if (instance) {
        instance->stopPulse();
    }
}

void HapticsDriver::stopPulse() {
    if (!_pulse_active) {
        return;
    }

    _pulse_active = false;

    // Stop vibration and return to internal trigger mode
    _drv.setRealtimeValue(0);
    _drv.setMode(DRV2605_MODE_INTTRIG);

    ESP_LOGD(TAG, "Pulse completed");
}
