/**
 * @file Adxl345.cpp
 * @brief ADXL345 accelerometer wrapper implementation
 */

#include "Adxl345.h"
#include "SharedI2cBus.h"
#include <esp_log.h>
#include <math.h>
#include <string.h>

static const char* TAG = "Adxl345";

Adxl345::Adxl345()
    : _bus(nullptr)
    , _address(ADXL345_DEFAULT_ADDRESS)
    , _sensor_id(12345)
    , _enabled(false) {
}

bool Adxl345::begin(SharedI2cBus* i2c_bus) {
    if (!retryInit(i2c_bus, 3)) {
        ESP_LOGE(TAG, "❌ ADXL345 initialization failed");
        _enabled = false;
        return false;
    }

    _enabled = true;

    if (!setRange(ADXL345_RANGE_16_G) ||
        !setDataRate(ADXL345_DATARATE_100_HZ) ||
        !writeRegisterInternal(ADXL345_REG_INT_ENABLE, 0x00) ||
        !writeRegisterInternal(ADXL345_REG_THRESH_TAP, calcGforce(14.0f)) ||
        !writeRegisterInternal(ADXL345_REG_DUR, calcDuration(30.0f)) ||
        !writeRegisterInternal(ADXL345_REG_LATENT, calcLatency(100.0f)) ||
        !writeRegisterInternal(ADXL345_REG_WINDOW, calcLatency(250.0f)) ||
        !writeRegisterInternal(ADXL345_REG_TAP_AXES, 0x0F) ||
        !writeRegisterInternal(ADXL345_REG_INT_MAP, 0x00) ||
        !writeRegisterInternal(ADXL345_REG_INT_ENABLE, 0x60) ||
        !writeRegisterInternal(ADXL345_REG_FIFO_CTL, 0x80 | 0x10)) {
        ESP_LOGE(TAG, "❌ ADXL345 configuration failed");
        _enabled = false;
        return false;
    }

    clearInterrupts();

    ESP_LOGI(TAG, "✅ ADXL345 initialized (range 16G, 100Hz)");

    return true;
}

bool Adxl345::getEvent(sensors_event_t* event) {
    if (!_enabled || !event) {
        return false;
    }

    int16_t raw_x = 0;
    int16_t raw_y = 0;
    int16_t raw_z = 0;
    if (!readRegister16(ADXL345_REG_DATAX0, &raw_x) ||
        !readRegister16(ADXL345_REG_DATAY0, &raw_y) ||
        !readRegister16(ADXL345_REG_DATAZ0, &raw_z)) {
        return false;
    }

    memset(event, 0, sizeof(sensors_event_t));
    event->version = sizeof(sensors_event_t);
    event->sensor_id = _sensor_id;
    event->type = SENSOR_TYPE_ACCELEROMETER;
    event->timestamp = millis();
    event->acceleration.x =
        raw_x * ADXL345_MG2G_MULTIPLIER * SENSORS_GRAVITY_STANDARD;
    event->acceleration.y =
        raw_y * ADXL345_MG2G_MULTIPLIER * SENSORS_GRAVITY_STANDARD;
    event->acceleration.z =
        raw_z * ADXL345_MG2G_MULTIPLIER * SENSORS_GRAVITY_STANDARD;
    return true;
}

uint8_t Adxl345::readRegister(uint8_t reg) {
    if (!_enabled) {
        ESP_LOGW(TAG, "Read register while sensor disabled");
        return 0;
    }

    uint8_t value = 0;
    if (!readRegisterInternal(reg, &value)) {
        ESP_LOGW(TAG, "Failed to read register 0x%02X", reg);
        return 0;
    }

    return value;
}

void Adxl345::clearInterrupts() {
    if (!_enabled) {
        return;
    }

    uint8_t ignored = 0;
    readRegisterInternal(ADXL345_REG_INT_SOURCE, &ignored);
    readRegisterInternal(ADXL345_REG_INT_SOURCE, &ignored);
}

int Adxl345::calculateCombinedMagnitude(float accel_x, float accel_y, float accel_z) {
    if (!_enabled) {
        return 0;
    }

    static float smoothed = 0.0f;
    static const float SMOOTHING_FACTOR = 0.1f;

    float raw = sqrtf((accel_x * accel_x) + (accel_y * accel_y) + (accel_z * accel_z));
    float dynamic = fabsf(raw - SENSORS_GRAVITY_EARTH);
    smoothed = (SMOOTHING_FACTOR * dynamic) + ((1.0f - SMOOTHING_FACTOR) * smoothed);

    return (int)roundf(smoothed);
}

uint8_t Adxl345::getFifoSampleCount() {
    if (!_enabled) {
        return 0;
    }

    uint8_t status = 0;
    if (!readRegisterInternal(ADXL345_REG_FIFO_STATUS, &status)) {
        return 0;
    }

    return status & 0x3F;
}

bool Adxl345::retryInit(SharedI2cBus* i2c_bus, uint8_t attempts) {
    if (!i2c_bus || !i2c_bus->isReady()) {
        ESP_LOGE(TAG, "❌ I2C bus not initialized");
        return false;
    }

    TwoWire* bus = i2c_bus->getBus();
    if (!bus) {
        ESP_LOGE(TAG, "❌ I2C bus not available");
        return false;
    }

    _bus = bus;
    _address = ADXL345_DEFAULT_ADDRESS;

    uint8_t retries_left = attempts;
    while (retries_left--) {
        bus->beginTransmission(ADXL345_DEFAULT_ADDRESS);
        byte error = bus->endTransmission();
        if (error != 0) {
            ESP_LOGW(TAG, "I2C error %d, retries left: %d", error, retries_left);
            if (retries_left > 0) {
                delay(500);
                continue;
            }
            return false;
        }

        uint8_t device_id = 0;
        if (readRegisterInternal(ADXL345_REG_DEVID, &device_id) &&
            device_id == 0xE5 &&
            writeRegisterInternal(ADXL345_REG_POWER_CTL, 0x08)) {
            uint8_t ignored = 0;
            readRegisterInternal(ADXL345_REG_INT_SOURCE, &ignored);
            readRegisterInternal(ADXL345_REG_INT_SOURCE, &ignored);
            return true;
        }

        ESP_LOGW(TAG, "ADXL345 begin failed, retries left: %d", retries_left);
        delay(500);
    }

    return false;
}

bool Adxl345::writeRegisterInternal(uint8_t reg, uint8_t value) {
    if (_bus == nullptr) {
        return false;
    }

    _bus->beginTransmission(_address);
    _bus->write(reg);
    _bus->write(value);
    return _bus->endTransmission() == 0;
}

bool Adxl345::readRegisterInternal(uint8_t reg, uint8_t* value) {
    if (_bus == nullptr || value == nullptr) {
        return false;
    }

    _bus->beginTransmission(_address);
    _bus->write(reg);
    if (_bus->endTransmission(false) != 0) {
        return false;
    }

    if (_bus->requestFrom(_address, static_cast<uint8_t>(1)) != 1) {
        return false;
    }

    *value = _bus->read();
    return true;
}

bool Adxl345::readRegister16(uint8_t reg, int16_t* value) {
    if (_bus == nullptr || value == nullptr) {
        return false;
    }

    _bus->beginTransmission(_address);
    _bus->write(reg);
    if (_bus->endTransmission(false) != 0) {
        return false;
    }

    if (_bus->requestFrom(_address, static_cast<uint8_t>(2)) != 2) {
        return false;
    }

    uint8_t lo = _bus->read();
    uint8_t hi = _bus->read();
    *value = static_cast<int16_t>((static_cast<uint16_t>(hi) << 8) | lo);
    return true;
}

bool Adxl345::setRange(adxl345_range_t range) {
    uint8_t format = 0;
    if (!readRegisterInternal(ADXL345_REG_DATA_FORMAT, &format)) {
        return false;
    }

    format &= ~0x0F;
    format |= static_cast<uint8_t>(range);
    format |= 0x08;
    return writeRegisterInternal(ADXL345_REG_DATA_FORMAT, format);
}

bool Adxl345::setDataRate(adxl345_data_rate_t data_rate) {
    return writeRegisterInternal(ADXL345_REG_BW_RATE,
                                 static_cast<uint8_t>(data_rate));
}

uint8_t Adxl345::calcGforce(float gforce) {
    return min((uint8_t)(gforce * 1000.0f / ADXL_FORCE_SCALE_FACTOR), (uint8_t)255);
}

uint8_t Adxl345::calcDuration(float duration_ms) {
    return min((uint8_t)(duration_ms / ADXL_DURATION_SCALE_FACTOR), (uint8_t)255);
}

uint8_t Adxl345::calcLatency(float latency_ms) {
    return min((uint8_t)(latency_ms / ADXL_LATENCY_SCALE_FACTOR), (uint8_t)255);
}
