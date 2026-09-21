/**
 * AudioCodec.cpp
 *
 * Implementation for AudioCodec.
 */

#include "AudioCodec.h"
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>

static const char* TAG = "AudioCodec";

// I2S microphone constructor (ICS-43434)
AudioCodec::AudioCodec(int input_sample_rate, int output_sample_rate,
                       int8_t spk_bclk, int8_t spk_lrck, int8_t spk_dout,
                       int8_t mic_i2s_data)
    : _input_sample_rate(input_sample_rate),
      _output_sample_rate(output_sample_rate),
      _output_volume(DEFAULT_OUTPUT_VOLUME),
      _input_gain(DEFAULT_INPUT_GAIN),
      _input_enabled(false),
      _output_enabled(false),
      _muted(false),
      _initialized(false),
      _mic_initialized(false),
      _spk_initialized(false),
      _i2s_started(false),
      _tx_channel(nullptr),
      _rx_channel(nullptr),
      _mic_i2s_data(mic_i2s_data),
      _spk_bclk(spk_bclk), _spk_lrck(spk_lrck), _spk_dout(spk_dout),
      _power_timer(nullptr),
      _last_input_time(0),
      _last_output_time(0),
      _i2s_read_buffer(nullptr),
      _i2s_write_buffer(nullptr),
      _max_buffer_samples(0) {
}

AudioCodec::~AudioCodec() {
    ESP_LOGI(TAG, "AudioCodec destructor: cleaning up resources");

    stopFullDuplexChannels();

    if (_tx_channel) {
        i2s_del_channel(_tx_channel);
        _tx_channel = nullptr;
    }
    if (_rx_channel) {
        i2s_del_channel(_rx_channel);
        _rx_channel = nullptr;
    }
    if (_mic_initialized || _spk_initialized) {
        _mic_initialized = false;
        _spk_initialized = false;
    }

    // Free pre-allocated PSRAM buffers
    if (_i2s_read_buffer) {
        ESP_LOGI(TAG, "Freeing I2S read buffer @ %p", _i2s_read_buffer);
        free(_i2s_read_buffer);
        _i2s_read_buffer = nullptr;
    }

    if (_i2s_write_buffer) {
        ESP_LOGI(TAG, "Freeing I2S write buffer @ %p", _i2s_write_buffer);
        free(_i2s_write_buffer);
        _i2s_write_buffer = nullptr;
    }

    // Delete power timer
    if (_power_timer) {
        esp_timer_stop(_power_timer);
        esp_timer_delete(_power_timer);
        _power_timer = nullptr;
    }

    ESP_LOGI(TAG, "AudioCodec cleanup complete");
}

bool AudioCodec::begin() {
    ESP_LOGI(TAG, "Initializing Audio Codec (Full-Duplex I2S)");

    // Initialize microphone and speaker (full-duplex)
    if (!initMicrophoneI2S()) {
        ESP_LOGE(TAG, "❌ Failed to initialize full-duplex I2S");
        return false;
    }

    // Pre-allocate I2S conversion buffers in PSRAM (zero-churn optimization)
    // Max buffer size: 2048 samples for generous headroom
    // - Read: typically 256 samples (I2S DMA buffer)
    // - Write: typically 1440 samples (60ms @ 24kHz)
    _max_buffer_samples = 2048;
    size_t buffer_size = _max_buffer_samples * 2 * sizeof(int32_t);  // stereo pairs

    ESP_LOGI(TAG, "Allocating I2S conversion buffers in PSRAM...");
    ESP_LOGI(TAG, "  Buffer size: %d KB each (%d samples × 2 channels × 4 bytes)",
             buffer_size / 1024, _max_buffer_samples);

    // Log memory before allocation
    size_t psram_free_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t internal_free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    _i2s_read_buffer = (int32_t*)heap_caps_malloc(
        buffer_size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    _i2s_write_buffer = (int32_t*)heap_caps_malloc(
        buffer_size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (!_i2s_read_buffer || !_i2s_write_buffer) {
        ESP_LOGE(TAG, "❌ Failed to allocate I2S conversion buffers in PSRAM");
        ESP_LOGE(TAG, "   PSRAM free: %d KB, Internal free: %d KB",
                 psram_free_before / 1024, internal_free_before / 1024);
        if (_i2s_read_buffer) {
            free(_i2s_read_buffer);
            _i2s_read_buffer = nullptr;
        }
        if (_i2s_write_buffer) {
            free(_i2s_write_buffer);
            _i2s_write_buffer = nullptr;
        }
        return false;
    }

    // Log memory after allocation
    size_t psram_free_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t internal_free_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    ESP_LOGI(TAG, "✅ I2S conversion buffers allocated in PSRAM:");
    ESP_LOGI(TAG, "   Read buffer:  %p (%d KB)", _i2s_read_buffer, buffer_size / 1024);
    ESP_LOGI(TAG, "   Write buffer: %p (%d KB)", _i2s_write_buffer, buffer_size / 1024);
    ESP_LOGI(TAG, "   Total allocated: %d KB", (buffer_size * 2) / 1024);
    ESP_LOGI(TAG, "   PSRAM:    %d KB → %d KB (used: %d KB)",
             psram_free_before / 1024, psram_free_after / 1024,
             (psram_free_before - psram_free_after) / 1024);
    ESP_LOGI(TAG, "   Internal: %d KB → %d KB (no change)",
             internal_free_before / 1024, internal_free_after / 1024);
    ESP_LOGI(TAG, "   Zero-churn optimization: ACTIVE ✓");

    // Create power management timer
    esp_timer_create_args_t timer_args = {
        .callback = &AudioCodec::powerTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "audio_power_timer",
        .skip_unhandled_events = true
    };

    esp_err_t ret = esp_timer_create(&timer_args, &_power_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create power timer: %s", esp_err_to_name(ret));
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "Audio codec initialization complete");

    return true;
}

bool AudioCodec::initMicrophoneI2S() {
    ESP_LOGD(TAG, "Configuring Full-Duplex I2S (Microphone + Speaker) on I2S0");
    ESP_LOGD(TAG, "  Mode: MASTER | RX | TX (full-duplex)");
    ESP_LOGD(TAG, "  Shared BCLK (GPIO%d) and WS (GPIO%d) for both mic and speaker", 
             _spk_bclk, _spk_lrck);
    ESP_LOGD(TAG, "  Mic DATA: GPIO%d, Speaker DATA: GPIO%d", _mic_i2s_data, _spk_dout);
    ESP_LOGD(TAG, "  Sample rate: %d Hz (shared for both)", _output_sample_rate);
    
    // Use speaker's sample rate (mic will be resampled in software)
    uint32_t sample_rate = (uint32_t)_output_sample_rate;
    
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_FULLDUPLEX,
                                                            I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &_tx_channel, &_rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to allocate full-duplex I2S channels: %s",
                 esp_err_to_name(ret));
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(_spk_bclk),
            .ws = static_cast<gpio_num_t>(_spk_lrck),
            .dout = static_cast<gpio_num_t>(_spk_dout),
            .din = static_cast<gpio_num_t>(_mic_i2s_data),
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(_tx_channel, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize I2S TX channel: %s",
                 esp_err_to_name(ret));
        return false;
    }

    ret = i2s_channel_init_std_mode(_rx_channel, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize I2S RX channel: %s",
                 esp_err_to_name(ret));
        return false;
    }

    _mic_initialized = true;
    _spk_initialized = true;  // Speaker is part of full-duplex setup
    
    ESP_LOGI(TAG, "✅ Full-duplex I2S configured successfully");
    ESP_LOGD(TAG, "  Hardware sample rate: %d Hz", sample_rate);
    ESP_LOGD(TAG, "  Mic target rate: %d Hz (will be resampled)", _input_sample_rate);
    ESP_LOGD(TAG, "  Bits per sample: 32-bit (mic: 24-bit audio in upper 24 bits)");
    ESP_LOGD(TAG, "  Channel format: Mono LEFT (ICS-43434 hardwired to LEFT channel)");
    ESP_LOGD(TAG, "  Microphone: ICS-43434 (I2S MEMS, LEFT channel)");
    
    return true;
}

bool AudioCodec::startFullDuplexChannels() {
    if (_i2s_started) {
        return true;
    }
    if (!_tx_channel || !_rx_channel) {
        ESP_LOGE(TAG, "❌ I2S channels not initialized");
        return false;
    }

    preloadSilence();

    esp_err_t ret = i2s_channel_enable(_tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to enable I2S TX channel: %s",
                 esp_err_to_name(ret));
        return false;
    }

    ret = i2s_channel_enable(_rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to enable I2S RX channel: %s",
                 esp_err_to_name(ret));
        i2s_channel_disable(_tx_channel);
        return false;
    }

    _i2s_started = true;
    delay(100);
    primeInputChannel();
    return true;
}

void AudioCodec::stopFullDuplexChannels() {
    if (!_i2s_started) {
        return;
    }

    if (_rx_channel) {
        esp_err_t ret = i2s_channel_disable(_rx_channel);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to disable I2S RX channel: %s",
                     esp_err_to_name(ret));
        }
    }
    if (_tx_channel) {
        esp_err_t ret = i2s_channel_disable(_tx_channel);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to disable I2S TX channel: %s",
                     esp_err_to_name(ret));
        }
    }

    _i2s_started = false;
}

void AudioCodec::primeInputChannel() {
    if (!_rx_channel) {
        return;
    }

    int32_t dummy_buffer[16];
    size_t bytes_read = 0;
    i2s_channel_read(_rx_channel, dummy_buffer, sizeof(dummy_buffer), &bytes_read, 10);
}

void AudioCodec::preloadSilence() {
    if (!_tx_channel || !_i2s_write_buffer || _max_buffer_samples == 0) {
        return;
    }

    size_t silence_frames = _max_buffer_samples > 256 ? 256 : _max_buffer_samples;
    size_t silence_bytes = silence_frames * 2 * sizeof(int32_t);
    memset(_i2s_write_buffer, 0, silence_bytes);

    size_t bytes_loaded = 0;
    esp_err_t ret = i2s_channel_preload_data(_tx_channel, _i2s_write_buffer,
                                             silence_bytes, &bytes_loaded);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2S silence preload skipped: %s", esp_err_to_name(ret));
    }
}

void AudioCodec::start() {
    if (!_initialized) {
        ESP_LOGW(TAG, "🟡 Audio codec not initialized");
        return;
    }

    setInputGain(DEFAULT_INPUT_GAIN);

    ESP_LOGI(TAG, "Starting audio channels");
    ESP_LOGD(TAG, "  Full-duplex I2S: Single port handles both mic (RX) and speaker (TX)");
    ESP_LOGD(TAG, "  Both channels can be active simultaneously");
    
    // Start full-duplex I2S port
    if (_mic_initialized && _spk_initialized) {
        if (startFullDuplexChannels()) {
            ESP_LOGD(TAG, "Full-duplex I2S started");
            _input_enabled = true;
            _output_enabled = !_muted;
        }
    }

    // Initialize timestamps
    _last_input_time = millis();
    _last_output_time = millis();

    // Start power management timer (check every 1 second)
    if (_power_timer) {
        esp_timer_start_periodic(_power_timer, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
    }

    ESP_LOGI(TAG, "Audio channels started");
}

void AudioCodec::stop() {
    if (!_initialized) {
        return;
    }

    ESP_LOGI(TAG, "Stopping audio channels");

    // Stop power management timer
    if (_power_timer) {
        esp_timer_stop(_power_timer);
    }

    // Full-duplex: single I2S port
    if (_mic_initialized && _spk_initialized) {
        stopFullDuplexChannels();
        _input_enabled = false;
        _output_enabled = false;
    }
}

void AudioCodec::setOutputVolume(int volume) {
    _output_volume = constrain(volume, 0, 100);
    ESP_LOGI(TAG, "Output volume set to %d%%", _output_volume);
}

void AudioCodec::setInputGain(float gain) {
    _input_gain = constrain(gain, 0.5f, 10.0f);
    ESP_LOGD(TAG, "Input gain set to %.2f", _input_gain);
}

void AudioCodec::enableInput(bool enable) {
    if (!_initialized || !_mic_initialized) {
        return;
    }

    // Full-duplex: input and output share the same port
    if (enable && !_input_enabled) {
        // Restart power timer
        if (_power_timer) {
            esp_timer_stop(_power_timer);
            esp_timer_start_periodic(_power_timer, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        }
        
        // Check if I2S is actually started
        if (!_i2s_started) {
            ESP_LOGD(TAG, "🟡  FIX: I2S not started, starting now (input: %d, output: %d)",
                     _input_enabled, _output_enabled);
            if (!startFullDuplexChannels()) {
                return;
            }
            _output_enabled = true;  // Output is also enabled (shared port)
        } else {
            ESP_LOGD(TAG, "🟡  FIX: I2S already running, not zeroing DMA (input: %d, output: %d)",
                     _input_enabled, _output_enabled);
        }
        
        _input_enabled = true;
        _last_input_time = millis();
    } else if (!enable && _input_enabled) {
        // Note: We can't stop RX without stopping TX in full-duplex mode
        // So we just mark input as disabled but keep I2S running for output
        _input_enabled = false;
        ESP_LOGI(TAG, "Microphone disabled (I2S continues for speaker)");
    }
}

void AudioCodec::enableOutput(bool enable) {
    if (!_initialized || !_spk_initialized) {
        return;
    }

    // Full-duplex: input and output share the same port
    if (enable && !_output_enabled) {
        if (_muted) {
            ESP_LOGI(TAG, "Speaker enable ignored (muted)");
            return;
        }
        if (_power_timer) {
            esp_timer_stop(_power_timer);
            esp_timer_start_periodic(_power_timer, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        }
        
        // Check if I2S is actually started
        if (!_i2s_started) {
            ESP_LOGD(TAG, "🟡  FIX: I2S not started, starting now (input: %d, output: %d)",
                     _input_enabled, _output_enabled);
            if (!startFullDuplexChannels()) {
                return;
            }
        }
        
        _output_enabled = true;
        _input_enabled = true;  // Input is also enabled (shared port)
        _last_output_time = millis();
        ESP_LOGI(TAG, "Speaker enabled (full-duplex mode - mic also active)");
    } else if (!enable && _output_enabled) {
        // Note: We can't stop TX without stopping RX in full-duplex mode
        // So we just mark output as disabled but keep I2S running for input
        _output_enabled = false;
        ESP_LOGI(TAG, "Speaker disabled (I2S continues for microphone)");
    }
}

void AudioCodec::setMuted(bool muted) {
    _muted = muted;
    if (muted) {
        enableOutput(false);
    }
}

int AudioCodec::read(int16_t* buffer, int samples) {
    if (!_initialized || !_mic_initialized || !buffer) {
        return 0;
    }

    // Auto-enable input if it was disabled
    if (!_input_enabled) {
        ESP_LOGW(TAG, "🟡 Read called but input disabled! Auto-enabling... (output enabled: %d)", _output_enabled);
        enableInput(true);
    }

    // Bounds check: ensure samples fit in pre-allocated buffer
    if (samples > _max_buffer_samples) {
        ESP_LOGW(TAG, "Read samples (%d) exceeds buffer size (%d), clamping",
                 samples, _max_buffer_samples);
        samples = _max_buffer_samples;
    }

    // Read stereo pairs (required for LEFT channel to work)
    size_t bytes_to_read = samples * 2 * sizeof(int32_t);
    size_t bytes_read = 0;

    // Use pre-allocated PSRAM buffer (zero-churn optimization)
    esp_err_t ret = i2s_channel_read(_rx_channel, _i2s_read_buffer, bytes_to_read,
                                     &bytes_read, 100);

    if (ret != ESP_OK) {
        if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "[Mic Debug] I2S read failed: %s", esp_err_to_name(ret));
        }
        return 0;  // No free() needed - buffer is persistent
    }

    if (bytes_read == 0) {
        return 0;  // No free() needed - buffer is persistent
    }

    int stereo_pairs = bytes_read / (2 * sizeof(int32_t));

    int64_t left_energy = 0;
    int64_t right_energy = 0;
    for (int i = 0; i < stereo_pairs && i < samples; i++) {
        int32_t raw_l = _i2s_read_buffer[i * 2];
        int32_t raw_r = _i2s_read_buffer[i * 2 + 1];

        // ICS-43434: 24-bit data in upper 24 bits.
        int32_t sample_l_24bit = (raw_l >> 8) & 0x00FFFFFF;
        if (sample_l_24bit & 0x00800000) {
            sample_l_24bit |= 0xFF000000;
        }

        int32_t sample_r_24bit = (raw_r >> 8) & 0x00FFFFFF;
        if (sample_r_24bit & 0x00800000) {
            sample_r_24bit |= 0xFF000000;
        }

        int16_t sample_l = static_cast<int16_t>(sample_l_24bit >> 8);
        int16_t sample_r = static_cast<int16_t>(sample_r_24bit >> 8);
        left_energy += abs(sample_l);
        right_energy += abs(sample_r);
    }

    const bool use_left_channel =
        left_energy > 0 && left_energy >= (right_energy * 2);
    const bool use_right_channel =
        right_energy > 0 && right_energy >= (left_energy * 2);

    for (int i = 0; i < stereo_pairs && i < samples; i++) {
        int32_t raw_l = _i2s_read_buffer[i * 2];
        int32_t raw_r = _i2s_read_buffer[i * 2 + 1];

        int32_t sample_l_24bit = (raw_l >> 8) & 0x00FFFFFF;
        if (sample_l_24bit & 0x00800000) {
            sample_l_24bit |= 0xFF000000;
        }

        int32_t sample_r_24bit = (raw_r >> 8) & 0x00FFFFFF;
        if (sample_r_24bit & 0x00800000) {
            sample_r_24bit |= 0xFF000000;
        }

        int16_t sample_l = static_cast<int16_t>(sample_l_24bit >> 8);
        int16_t sample_r = static_cast<int16_t>(sample_r_24bit >> 8);

        int32_t sample = 0;
        if (use_left_channel) {
            sample = sample_l;
        } else if (use_right_channel) {
            sample = sample_r;
        } else {
            // If both channels carry similar energy, blend rather than double.
            sample = (static_cast<int32_t>(sample_l) +
                      static_cast<int32_t>(sample_r)) / 2;
        }

        if (_input_gain != 1.0f) {
            sample = static_cast<int32_t>(sample * _input_gain);
        }

        buffer[i] = constrain(sample, INT16_MIN, INT16_MAX);
    }

    // No free() needed - buffer is pre-allocated and reused (zero-churn optimization)
    _last_input_time = millis();
    return stereo_pairs;
}

int AudioCodec::getActualInputSampleRate() const {
    // Full-duplex I2S: hardware runs at speaker rate (24kHz)
    return _output_sample_rate;
}

int AudioCodec::write(const int16_t* buffer, int samples) {
    if (!_initialized) {
        ESP_LOGW(TAG, "🟡 Write failed: codec not initialized");
        return 0;
    }
    if (!_spk_initialized) {
        ESP_LOGW(TAG, "🟡 Write failed: speaker not initialized");
        return 0;
    }
    if (!buffer) {
        ESP_LOGW(TAG, "🟡 Write failed: null buffer");
        return 0;
    }
    if (_muted) {
        return 0;
    }

    // Auto-enable output if it was disabled
    if (!_output_enabled) {
        ESP_LOGW(TAG, "🟡 Write called but output disabled! Auto-enabling... (input enabled: %d)", _input_enabled);
        enableOutput(true);
    }

    // Bounds check: ensure samples fit in pre-allocated buffer
    if (samples > _max_buffer_samples) {
        ESP_LOGW(TAG, "Write samples (%d) exceeds buffer size (%d), clamping",
                 samples, _max_buffer_samples);
        samples = _max_buffer_samples;
    }

    // Full-duplex stereo mode: duplicate mono to both L+R channels
    size_t stereo_size = samples * 2 * sizeof(int32_t);

    // Use pre-allocated PSRAM buffer (zero-churn optimization)
    // Apply volume and duplicate to both channels
    for (int i = 0; i < samples; i++) {
        int32_t temp = (buffer[i] * _output_volume) / 100;
        int16_t sample = constrain(temp, INT16_MIN, INT16_MAX);
        int32_t sample_32 = ((int32_t)sample) << 16;

        _i2s_write_buffer[i * 2] = sample_32;      // LEFT
        _i2s_write_buffer[i * 2 + 1] = sample_32;  // RIGHT (duplicate)
    }

    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(_tx_channel, _i2s_write_buffer, stereo_size,
                                      &bytes_written, 1000);

    // No free() needed - buffer is pre-allocated and reused (zero-churn optimization)

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ I2S write failed: %s", esp_err_to_name(ret));
        return 0;
    }

    _last_output_time = millis();
    return bytes_written / (2 * sizeof(int32_t));
}

// Power management timer callback
void AudioCodec::powerTimerCallback(void* arg) {
    AudioCodec* codec = static_cast<AudioCodec*>(arg);
    codec->checkAndUpdatePowerState();
}

// Keep output alive (reset timer to prevent auto-disable during listening)
void AudioCodec::keepOutputAlive() {
    if (_output_enabled) {
        _last_output_time = millis();
    }
}

// Check and update power state based on inactivity
void AudioCodec::checkAndUpdatePowerState() {
    unsigned long now = millis();
    unsigned long input_elapsed = now - _last_input_time;

    // ⚠️ QUICK TEST: Power management TEMPORARILY DISABLED for debugging
    // This will help identify if power management is causing mic issues during playback

    // Auto-disable input after timeout (power saving for microphone)
    // DISABLED FOR TESTING
    // if (input_elapsed > AUDIO_POWER_TIMEOUT_MS && _input_enabled) {
    //     ESP_LOGI(TAG, "Auto-disabling microphone after %lu ms inactivity", input_elapsed);
    //     enableInput(false);
    // }

    // Note: Output (speaker) is NOT auto-disabled to ensure immediate TTS playback
    // The speaker stays enabled once started to avoid delays when TTS begins

    // Stop timer if input is disabled (output stays enabled, so timer continues)
    // DISABLED FOR TESTING
    // if (!_input_enabled && !_output_enabled && _power_timer) {
    //     esp_timer_stop(_power_timer);
    //     ESP_LOGD(TAG, "Power timer stopped (both channels idle)");
    // }
}
