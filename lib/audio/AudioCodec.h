/**
 * AudioCodec.h
 *
 * Declarations for AudioCodec.
 */

#pragma once

// System includes
#include <Arduino.h>

// ESP-IDF includes
#include <driver/i2s_std.h>
#include <esp_timer.h>

// Constants

/**
 * @brief interval.
 * Audio power timeout after inactivity (30 seconds)
 */
#define AUDIO_POWER_TIMEOUT_MS 30000

/**
 * Power state check interval (1 second)
 */
#define AUDIO_POWER_CHECK_INTERVAL_MS 1000

/**
 * AudioCodec - ESP32-S3 audio input/output driver
 *
 * Features:
 * - Full-duplex I2S audio (microphone + speaker)
 * - ICS-43434 I2S microphone support
 * - MAX98357A I2S speaker amplifier support
 * - Configurable logical input/output sample rates
 * - Volume and gain control
 * - Automatic power management
 *
 * Architecture:
 * - Dual I2S ports (I2S0 for output, I2S1 for input)
 * - Auto-disable output after 30s of inactivity
 * - Software gain control for microphone
 * - Hardware volume control for speaker
 *
 * Hardware:
 * - ESP32-S3 I2S0 (speaker output)
 * - ESP32-S3 I2S1 (microphone input)
 * - ICS-43434 MEMS microphone
 * - MAX98357A amplifier
 */
class AudioCodec {
public:
    static constexpr float DEFAULT_INPUT_GAIN = 12.0f;
    static constexpr int DEFAULT_OUTPUT_VOLUME = 100;
    static constexpr i2s_port_t I2S_NUM_FULLDUPLEX = I2S_NUM_0;

    /**
     * @brief Construct audio codec instance for I2S microphone and speaker
     *
     * @param input_sample_rate Input sample rate in Hz (typically 16000)
     * @param output_sample_rate Output sample rate in Hz (typically 16000)
     * @param spk_bclk Speaker I2S bit clock pin
     * @param spk_lrck Speaker I2S left/right clock pin
     * @param spk_dout Speaker I2S data out pin
     * @param mic_i2s_data Microphone I2S data in pin
     */
    AudioCodec(int input_sample_rate, int output_sample_rate,
               int8_t spk_bclk, int8_t spk_lrck, int8_t spk_dout,
               int8_t mic_i2s_data);

    /**
     * @brief Destructor - cleanup I2S buffers and resources
     */
    ~AudioCodec();

    /**
     * @brief Initialize I2S hardware and audio subsystem
     *
     * @return true on success, false on failure
     */
    bool begin();

    /**
     * @brief Start audio input/output operation
     */
    void start();

    /**
     * @brief Stop audio input/output operation
     */
    void stop();

    /**
     * @brief Check if audio codec is initialized
     *
     * @return true if ready, false otherwise
     */
    bool isReady() const { return _initialized; }

    /**
     * @brief Set speaker output volume
     *
     * @param volume Volume level (0-100, where 0 is mute, 100 is maximum)
     */
    void setOutputVolume(int volume);

    /**
     * @brief Set microphone input gain
     *
     * @param gain Amplification factor (1.0 = no gain, 2.0 = double, etc.)
     */
    void setInputGain(float gain);

    /**
     * @brief Enable or disable microphone input
     *
     * @param enable true to enable input, false to disable
     */
    void enableInput(bool enable);

    /**
     * @brief Enable or disable speaker output
     *
     * @param enable true to enable output, false to disable
     */
    void enableOutput(bool enable);

    /**
     * @brief Mute or unmute audio output
     *
     * @param muted true to mute, false to unmute
     */
    void setMuted(bool muted);

    /**
     * @brief Check if audio output is muted
     *
     * @return true if muted, false otherwise
     */
    bool isMuted() const { return _muted; }

    /**
     * @brief Reset output power timer to prevent auto-disable
     *
     * Call this during audio playback to keep output active.
     */
    void keepOutputAlive();

    /**
     * @brief Read PCM samples from microphone
     *
     * @param buffer Output buffer for PCM samples
     * @param samples Number of samples to read
     * @return Number of samples actually read, or negative on error
     */
    int read(int16_t* buffer, int samples);

    /**
     * @brief Write PCM samples to speaker
     *
     * @param buffer Input buffer with PCM samples
     * @param samples Number of samples to write
     * @return Number of samples actually written, or negative on error
     */
    int write(const int16_t* buffer, int samples);

    /**
     * @brief Get configured input sample rate
     *
     * @return Input sample rate in Hz
     */
    int getInputSampleRate() const { return _input_sample_rate; }

    /**
     * @brief Get configured output sample rate
     *
     * @return Output sample rate in Hz
     */
    int getOutputSampleRate() const { return _output_sample_rate; }

    /**
     * @brief Get actual hardware input sample rate
     *
     * May differ from configured rate due to hardware limitations.
     *
     * @return Actual hardware sample rate in Hz
     */
    int getActualInputSampleRate() const;

    /**
     * @brief Get current output volume
     *
     * @return Volume level (0-100)
     */
    int getOutputVolume() const { return _output_volume; }

    /**
     * @brief Get current input gain
     *
     * @return Gain amplification factor
     */
    float getInputGain() const { return _input_gain; }

    /**
     * @brief Check if input is enabled
     *
     * @return true if enabled, false otherwise
     */
    bool isInputEnabled() const { return _input_enabled; }

    /**
     * @brief Check if output is enabled
     *
     * @return true if enabled, false otherwise
     */
    bool isOutputEnabled() const { return _output_enabled; }

private:
    /**
     * @brief Initialize full-duplex I2S for microphone and speaker
     *
     * @return true on success, false on failure
     */
    bool initMicrophoneI2S();
    bool startFullDuplexChannels();
    void stopFullDuplexChannels();
    void primeInputChannel();
    void preloadSilence();

    /**
     * @brief Check inactivity and update power state
     */
    void checkAndUpdatePowerState();

    /**
     * @brief Power timer callback for automatic power management
     *
     * @param arg Pointer to AudioCodec instance
     */
    static void powerTimerCallback(void* arg);

    // Configuration
    int _input_sample_rate;
    int _output_sample_rate;
    int _output_volume;
    float _input_gain;
    bool _input_enabled;
    bool _output_enabled;
    bool _muted;
    bool _initialized;
    bool _mic_initialized;
    bool _spk_initialized;
    bool _i2s_started;
    i2s_chan_handle_t _tx_channel;
    i2s_chan_handle_t _rx_channel;

    // Pin configuration
    int8_t _spk_bclk, _spk_lrck, _spk_dout;
    int8_t _mic_i2s_data;

    // Storage

    // Power management
    esp_timer_handle_t _power_timer;
    unsigned long _last_input_time;
    unsigned long _last_output_time;

    // Pre-allocated I2S conversion buffers (PSRAM)
    int32_t* _i2s_read_buffer;
    int32_t* _i2s_write_buffer;
    size_t _max_buffer_samples;
};
