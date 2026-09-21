/**
 * Mp3Player.cpp
 *
 * Implementation for Mp3Player.
 */

#include "Mp3Player.h"
#include "Mp3Decoder.h"
#include "LittleFsAdapter.h"
#include "RuntimeTaskRegistry.h"
#include <FS.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>

static const char* TAG = "Mp3Player";

namespace {
int calculateResampledSamples(int input_samples, int input_rate,
                              int output_rate) {
    if (input_samples <= 0 || input_rate <= 0 || output_rate <= 0) {
        return 0;
    }

    return static_cast<int>(
        (static_cast<int64_t>(input_samples) * output_rate + input_rate - 1) /
        input_rate);
}

void resamplePcmLinear(const int16_t* input,
                       int input_samples,
                       int input_rate,
                       int16_t* output,
                       int output_samples,
                       int output_rate) {
    if (!input || !output || input_samples <= 0 || output_samples <= 0) {
        return;
    }

    if (input_rate == output_rate || input_samples == output_samples) {
        int samples_to_copy = min(input_samples, output_samples);
        memcpy(output, input, samples_to_copy * sizeof(int16_t));
        if (output_samples > samples_to_copy) {
            memset(output + samples_to_copy,
                   0,
                   (output_samples - samples_to_copy) * sizeof(int16_t));
        }
        return;
    }

    int64_t position_q16 = 0;
    int64_t step_q16 =
        (static_cast<int64_t>(input_rate) << 16) / output_rate;

    for (int i = 0; i < output_samples; ++i) {
        int index = static_cast<int>(position_q16 >> 16);
        int frac = static_cast<int>(position_q16 & 0xFFFF);
        int next_index = min(index + 1, input_samples - 1);

        int32_t sample_a = input[min(index, input_samples - 1)];
        int32_t sample_b = input[next_index];
        int32_t blended =
            ((sample_a * (65536 - frac)) + (sample_b * frac)) >> 16;

        output[i] = static_cast<int16_t>(blended);
        position_q16 += step_q16;
    }
}
}  // namespace

Mp3Player::Mp3Player(AudioCodec* codec, LittleFsAdapter* filesystem)
    : _codec(codec)
    , _filesystem(filesystem)
    , _state_mutex(nullptr)
    , _command_queue(nullptr)
    , _is_playing(false)
    , _stop_requested(false)
    , _playback_pending(false)
    , _worker_running(false)
    , _worker_busy(false)
{
    _playback_data.mp3_buffer = nullptr;
    _playback_data.mp3_size = 0;
    _playback_data.path = nullptr;
    _playback_data.repeat_count = 1;
    _playback_data.owns_buffer = false;
    _playback_data.buffer_in_psram = false;
    _pcm_buffer = nullptr;
    _pcm_buffer_in_psram = false;
    _resample_buffer = nullptr;
    _resample_buffer_in_psram = false;
    _resample_buffer_capacity = 0;
    _decoder_ready = false;
    _last_activity_ms = millis();
    _last_open_error_path[0] = '\0';
    _last_open_error_ms = 0;
    _current_playback_path[0] = '\0';
    for (size_t i = 0; i < CACHE_SLOTS; ++i) {
        _cache[i].path[0] = '\0';
        _cache[i].buffer = nullptr;
        _cache[i].size = 0;
        _cache[i].last_used_ms = 0;
        _cache[i].in_use = false;
        _cache[i].in_psram = false;
    }
    _codec_mutex = xSemaphoreCreateMutex();
    _state_mutex = xSemaphoreCreateMutex();
    _command_queue = xQueueCreate(COMMAND_QUEUE_DEPTH, sizeof(PlayCommand));
    if (!_state_mutex) {
        ESP_LOGE(TAG, "❌ Failed to create MP3 state mutex");
    }
    if (!_command_queue) {
        ESP_LOGE(TAG, "❌ Failed to create MP3 command queue");
    }
    ESP_LOGD(TAG, "Mp3Player created");
}

Mp3Player::~Mp3Player() {
    ESP_LOGD(TAG, "Mp3Player destructor");
    _worker_running = false;
    _stop_requested = true;
    _playback_pending = false;

    if (_command_queue) {
        xQueueReset(_command_queue);
        PlayCommand wake_command = {};
        xQueueOverwrite(_command_queue, &wake_command);
    }

    if (RuntimeTaskRegistry::instance().isTaskActive("mp3_play")) {
        RuntimeTaskRegistry::instance().stopTask("mp3_play");
    }

    clearPlaybackData();

    for (size_t i = 0; i < CACHE_SLOTS; ++i) {
        if (_cache[i].in_use) {
            releaseCacheEntry(&_cache[i]);
        }
    }
    if (_decoder_ready) {
        MP3Decoder_FreeBuffers();
        _decoder_ready = false;
    }
    if (_pcm_buffer) {
        if (_pcm_buffer_in_psram) {
            heap_caps_free(_pcm_buffer);
        } else {
            free(_pcm_buffer);
        }
        _pcm_buffer = nullptr;
        _pcm_buffer_in_psram = false;
    }
    if (_resample_buffer) {
        if (_resample_buffer_in_psram) {
            heap_caps_free(_resample_buffer);
        } else {
            free(_resample_buffer);
        }
        _resample_buffer = nullptr;
        _resample_buffer_in_psram = false;
        _resample_buffer_capacity = 0;
    }
    if (_command_queue) {
        vQueueDelete(_command_queue);
        _command_queue = nullptr;
    }
    if (_state_mutex) {
        vSemaphoreDelete(_state_mutex);
        _state_mutex = nullptr;
    }
    if (_codec_mutex) vSemaphoreDelete(_codec_mutex);
}

bool Mp3Player::ensureDecoderBuffers() {
    if (_decoder_ready) {
        return true;
    }
    if (!MP3Decoder_AllocateBuffers()) {
        ESP_LOGE(TAG, "❌ Failed to allocate MP3 decoder buffers");
        return false;
    }
    _decoder_ready = true;
    return true;
}

bool Mp3Player::ensurePcmBuffer() {
    if (_pcm_buffer) {
        return true;
    }

    size_t pcm_buffer_size = m_MAX_NSAMP * m_MAX_NCHAN * sizeof(int16_t);
    _pcm_buffer = static_cast<int16_t*>(
        heap_caps_malloc(pcm_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    _pcm_buffer_in_psram = (_pcm_buffer != nullptr);
    if (!_pcm_buffer) {
        _pcm_buffer = static_cast<int16_t*>(malloc(pcm_buffer_size));
        _pcm_buffer_in_psram = false;
    }

    if (!_pcm_buffer) {
        ESP_LOGE(TAG, "❌ Failed to allocate PCM buffer");
        return false;
    }
    return true;
}

void Mp3Player::releaseIdleResources() {
    if (_decoder_ready) {
        MP3Decoder_FreeBuffers();
        _decoder_ready = false;
    }
    if (_pcm_buffer) {
        if (_pcm_buffer_in_psram) {
            heap_caps_free(_pcm_buffer);
        } else {
            free(_pcm_buffer);
        }
        _pcm_buffer = nullptr;
        _pcm_buffer_in_psram = false;
    }
    if (_resample_buffer) {
        if (_resample_buffer_in_psram) {
            heap_caps_free(_resample_buffer);
        } else {
            free(_resample_buffer);
        }
        _resample_buffer = nullptr;
        _resample_buffer_in_psram = false;
        _resample_buffer_capacity = 0;
    }
}

bool Mp3Player::ensureWorkerTask() {
    if (RuntimeTaskRegistry::instance().isTaskActive("mp3_play")) {
        return true;
    }

    _worker_running = true;
    bool created = RuntimeTaskRegistry::instance().createTask(
        "mp3_play",
        "Mp3Player",
        playbackTask,
        this,
        2,
        1,
        4096,
        CleanupPattern::GRACEFUL_THEN_FORCE,
        "Persistent MP3 playback worker",
        250
    );
    if (!created) {
        _worker_running = false;
        return false;
    }

    return true;
}

bool Mp3Player::validatePlaybackRequest(const char* path) {
    if (!_filesystem) {
        ESP_LOGE(TAG, "❌ Filesystem not available");
        return false;
    }

    File file = _filesystem->open(path, "r");
    if (!file) {
        uint32_t now = millis();
        bool path_changed = (strncmp(_last_open_error_path,
                                     path,
                                     sizeof(_last_open_error_path)) != 0);
        bool interval_elapsed =
            path_changed ||
            ((uint32_t)(now - _last_open_error_ms) >= OPEN_ERROR_LOG_INTERVAL_MS);
        if (interval_elapsed) {
            strncpy(_last_open_error_path, path, sizeof(_last_open_error_path) - 1);
            _last_open_error_path[sizeof(_last_open_error_path) - 1] = '\0';
            _last_open_error_ms = now;
            ESP_LOGE(TAG, "❌ Failed to open file: %s", path);
        }
        return false;
    }

    size_t file_size = file.size();
    file.close();

    if (file_size > MAX_FILE_SIZE) {
        ESP_LOGE(TAG, "❌ File too large: %d bytes (max %d)", file_size, MAX_FILE_SIZE);
        return false;
    }

    return true;
}

bool Mp3Player::validateSequenceRequest(const char* const* paths, size_t path_count) {
    if (!paths || path_count == 0 || path_count > MAX_SEQUENCE_ITEMS) {
        return false;
    }

    for (size_t i = 0; i < path_count; ++i) {
        if (!paths[i] || paths[i][0] == '\0') {
            ESP_LOGW(TAG, "🟡 Empty playback path in sequence index %u",
                     static_cast<unsigned int>(i));
            return false;
        }

        if (!validatePlaybackRequest(paths[i])) {
            return false;
        }
    }

    return true;
}

uint8_t* Mp3Player::allocateMp3Buffer(size_t size, bool prefer_psram, bool* used_psram) {
    if (used_psram) {
        *used_psram = false;
    }
    if (prefer_psram) {
        uint8_t* buffer = static_cast<uint8_t*>(
            heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
        if (buffer) {
            if (used_psram) {
                *used_psram = true;
            }
            return buffer;
        }
    }
    return static_cast<uint8_t*>(malloc(size));
}

void Mp3Player::freeMp3Buffer(uint8_t* buffer, bool in_psram) {
    if (!buffer) {
        return;
    }
    if (in_psram) {
        heap_caps_free(buffer);
        return;
    }
    free(buffer);
}

Mp3Player::CacheEntry* Mp3Player::findCacheEntry(const char* path) {
    if (!path) {
        return nullptr;
    }
    for (size_t i = 0; i < CACHE_SLOTS; ++i) {
        if (_cache[i].in_use && strcmp(_cache[i].path, path) == 0) {
            return &_cache[i];
        }
    }
    return nullptr;
}

bool Mp3Player::isPlaybackUsingCacheEntry(const CacheEntry* entry) const {
    return entry &&
           entry->in_use &&
           !_playback_data.owns_buffer &&
           _playback_data.mp3_buffer == entry->buffer;
}

Mp3Player::CacheEntry* Mp3Player::allocateCacheEntry() {
    CacheEntry* free_slot = nullptr;
    CacheEntry* oldest = nullptr;
    for (size_t i = 0; i < CACHE_SLOTS; ++i) {
        if (!_cache[i].in_use) {
            free_slot = &_cache[i];
            break;
        }
        if (isPlaybackUsingCacheEntry(&_cache[i])) {
            // Shared cue preloads can overlap with active startup playback.
            // Evicting the live cache entry corrupts the current sound, so
            // active playback buffers must stay pinned.
            continue;
        }
        if (!oldest || _cache[i].last_used_ms < oldest->last_used_ms) {
            oldest = &_cache[i];
        }
    }

    if (free_slot) {
        return free_slot;
    }
    if (oldest) {
        releaseCacheEntry(oldest);
        return oldest;
    }
    return nullptr;
}

void Mp3Player::releaseCacheEntry(CacheEntry* entry) {
    if (!entry) {
        return;
    }
    if (entry->buffer) {
        freeMp3Buffer(entry->buffer, entry->in_psram);
    }
    entry->buffer = nullptr;
    entry->size = 0;
    entry->last_used_ms = 0;
    entry->path[0] = '\0';
    entry->in_use = false;
    entry->in_psram = false;
}

bool Mp3Player::playFile(const char* path,
                         uint8_t repeat_count,
                         PlaybackPolicy policy) {
    uint8_t normalized_repeat_count = repeat_count > 0 ? repeat_count : 1;
    if (!path || path[0] == '\0') {
        ESP_LOGW(TAG, "🟡 Empty playback path");
        return false;
    }

    ESP_LOGI(TAG, "playFile() called: %s (repeat=%u)", path, normalized_repeat_count);

    if (!_codec || !_codec->isReady()) {
        ESP_LOGW(TAG, "🟡 Codec not ready");
        return false;
    }
    if (_codec->isMuted()) {
        ESP_LOGW(TAG, "🟡 Audio muted, skipping playback");
        return false;
    }

    if (!_command_queue) {
        ESP_LOGE(TAG, "❌ MP3 command queue not available");
        return false;
    }
    const char* paths[] = {path};
    if (!validateSequenceRequest(paths, 1)) {
        return false;
    }

    if (!ensureWorkerTask()) {
        ESP_LOGE(TAG, "❌ Failed to start playback worker");
        return false;
    }
    if (policy == PlaybackPolicy::ONLY_IF_IDLE &&
        (_worker_busy || _is_playing || _playback_pending)) {
        ESP_LOGI(TAG, "Skipping playback because player is busy: %s", path);
        return false;
    }

    PlayCommand command = {};
    strncpy(command.paths[0], path, sizeof(command.paths[0]) - 1);
    command.paths[0][sizeof(command.paths[0]) - 1] = '\0';
    command.path_count = 1;
    command.repeat_count = normalized_repeat_count;

    _playback_pending = true;
    _last_activity_ms = millis();
    if (policy == PlaybackPolicy::INTERRUPT_CURRENT &&
        (_worker_busy || _is_playing)) {
        _stop_requested = true;
    }

    xQueueOverwrite(_command_queue, &command);
    return true;
}

bool Mp3Player::playSequence(const char* const* paths,
                             size_t path_count,
                             PlaybackPolicy policy) {
    if (!paths || path_count == 0 || path_count > MAX_SEQUENCE_ITEMS) {
        ESP_LOGW(TAG, "🟡 Invalid playback sequence request");
        return false;
    }

    if (!_codec || !_codec->isReady()) {
        ESP_LOGW(TAG, "🟡 Codec not ready");
        return false;
    }
    if (_codec->isMuted()) {
        ESP_LOGW(TAG, "🟡 Audio muted, skipping sequence playback");
        return false;
    }
    if (!_command_queue) {
        ESP_LOGE(TAG, "❌ MP3 command queue not available");
        return false;
    }
    if (!validateSequenceRequest(paths, path_count)) {
        return false;
    }
    if (!ensureWorkerTask()) {
        ESP_LOGE(TAG, "❌ Failed to start playback worker");
        return false;
    }
    if (policy == PlaybackPolicy::ONLY_IF_IDLE &&
        (_worker_busy || _is_playing || _playback_pending)) {
        ESP_LOGI(TAG, "Skipping sequence playback because player is busy");
        return false;
    }

    PlayCommand command = {};
    command.path_count = static_cast<uint8_t>(path_count);
    command.repeat_count = 1;
    for (size_t i = 0; i < path_count; ++i) {
        strncpy(command.paths[i], paths[i], sizeof(command.paths[i]) - 1);
        command.paths[i][sizeof(command.paths[i]) - 1] = '\0';
    }

    _playback_pending = true;
    _last_activity_ms = millis();
    if (policy == PlaybackPolicy::INTERRUPT_CURRENT &&
        (_worker_busy || _is_playing)) {
        _stop_requested = true;
    }

    xQueueOverwrite(_command_queue, &command);
    return true;
}

bool Mp3Player::getCurrentPlaybackPath(char* out_path, size_t out_size) const {
    if (!out_path || out_size == 0) {
        return false;
    }

    out_path[0] = '\0';
    if (!_state_mutex) {
        return false;
    }

    if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return false;
    }

    bool has_path = _current_playback_path[0] != '\0';
    if (has_path) {
        strncpy(out_path, _current_playback_path, out_size - 1);
        out_path[out_size - 1] = '\0';
    }
    xSemaphoreGive(_state_mutex);

    return has_path;
}

bool Mp3Player::preloadFile(const char* path) {
    if (!path || !_filesystem) {
        return false;
    }

    CacheEntry* cached = findCacheEntry(path);
    if (cached) {
        cached->last_used_ms = millis();
        return true;
    }

    File file = _filesystem->open(path, "r");
    if (!file) {
        ESP_LOGW(TAG, "Failed to preload missing file: %s", path);
        return false;
    }

    size_t file_size = file.size();
    if (file_size > MAX_FILE_SIZE) {
        file.close();
        return false;
    }

    CacheEntry* cache_slot = nullptr;
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > file_size) {
        cache_slot = allocateCacheEntry();
    }
    if (!cache_slot) {
        file.close();
        return false;
    }

    bool used_psram = false;
    uint8_t* buffer = allocateMp3Buffer(file_size, true, &used_psram);
    if (!buffer || !used_psram) {
        if (buffer) {
            freeMp3Buffer(buffer, used_psram);
        }
        file.close();
        return false;
    }

    size_t read_bytes = file.read(buffer, file_size);
    file.close();
    if (read_bytes != file_size) {
        freeMp3Buffer(buffer, used_psram);
        return false;
    }

    cache_slot->buffer = buffer;
    cache_slot->size = file_size;
    cache_slot->last_used_ms = millis();
    cache_slot->in_use = true;
    cache_slot->in_psram = used_psram;
    strncpy(cache_slot->path, path, sizeof(cache_slot->path) - 1);
    cache_slot->path[sizeof(cache_slot->path) - 1] = '\0';
    return true;
}

bool Mp3Player::loadPlaybackData(const char* path, uint8_t repeat_count) {
    clearPlaybackData();

    // Check available memory before starting. If decoder/PCM buffers already exist,
    // allow playback with low headroom to keep short loop sounds working.
    size_t free_mem = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (free_mem < 30720) {
        if (!_decoder_ready || !_pcm_buffer) {
            ESP_LOGE(TAG, "❌ Insufficient memory: %d bytes free (need 30KB)", free_mem);
            return false;
        }
        ESP_LOGW(TAG, "🟡 Low internal memory: %d bytes free, reusing buffers", free_mem);
    }

    File file = _filesystem->open(path, "r");
    if (!file) {
        ESP_LOGE(TAG, "❌ Failed to open file: %s", path);
        return false;
    }

    size_t file_size = file.size();
    ESP_LOGD(TAG, "MP3 file size: %d bytes", file_size);

    CacheEntry* cached = findCacheEntry(path);
    if (cached && cached->size == file_size) {
        cached->last_used_ms = millis();
        _playback_data.mp3_buffer = cached->buffer;
        _playback_data.mp3_size = cached->size;
        _playback_data.path = cached->path;
        _playback_data.repeat_count = repeat_count;
        _playback_data.owns_buffer = false;
        _playback_data.buffer_in_psram = cached->in_psram;
        file.close();
        return true;
    }

    if (cached && cached->size != file_size) {
        releaseCacheEntry(cached);
        cached = nullptr;
    }

    CacheEntry* cache_slot = nullptr;
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > file_size) {
        cache_slot = allocateCacheEntry();
    }

    bool cached_buffer = false;
    if (cache_slot) {
        bool used_psram = false;
        uint8_t* buffer = allocateMp3Buffer(file_size, true, &used_psram);
        if (buffer && used_psram) {
            cache_slot->buffer = buffer;
            cache_slot->size = file_size;
            cache_slot->last_used_ms = millis();
            cache_slot->in_use = true;
            cache_slot->in_psram = true;
            strncpy(cache_slot->path, path, sizeof(cache_slot->path) - 1);
            cache_slot->path[sizeof(cache_slot->path) - 1] = '\0';
            cached_buffer = true;
        } else if (buffer) {
            freeMp3Buffer(buffer, used_psram);
        }
    }

    if (cached_buffer) {
        size_t read_bytes = file.read(cache_slot->buffer, file_size);
        file.close();
        if (read_bytes != file_size) {
            ESP_LOGE(TAG, "❌ Failed to read cached MP3 file");
            releaseCacheEntry(cache_slot);
            return false;
        }
        _playback_data.mp3_buffer = cache_slot->buffer;
        _playback_data.mp3_size = file_size;
        _playback_data.path = cache_slot->path;
        _playback_data.repeat_count = repeat_count;
        _playback_data.owns_buffer = false;
        _playback_data.buffer_in_psram = true;
        return true;
    }

    bool used_psram = false;
    uint8_t* buffer = allocateMp3Buffer(file_size, true, &used_psram);
    if (!buffer) {
        ESP_LOGE(TAG, "❌ Failed to allocate %d bytes", file_size);
        file.close();
        return false;
    }

    size_t read_bytes = file.read(buffer, file_size);
    file.close();
    if (read_bytes != file_size) {
        ESP_LOGE(TAG, "❌ Failed to read MP3 file");
        freeMp3Buffer(buffer, used_psram);
        return false;
    }

    _playback_data.mp3_buffer = buffer;
    _playback_data.mp3_size = file_size;
    _playback_data.path = nullptr;
    _playback_data.repeat_count = repeat_count;
    _playback_data.owns_buffer = true;
    _playback_data.buffer_in_psram = used_psram;
    return true;
}

void Mp3Player::clearPlaybackData() {
    if (_playback_data.mp3_buffer && _playback_data.owns_buffer) {
        freeMp3Buffer(_playback_data.mp3_buffer, _playback_data.buffer_in_psram);
    }
    _playback_data.mp3_buffer = nullptr;
    _playback_data.mp3_size = 0;
    _playback_data.path = nullptr;
    _playback_data.repeat_count = 1;
    _playback_data.owns_buffer = false;
    _playback_data.buffer_in_psram = false;
}

void Mp3Player::updateCurrentPlaybackPath(const char* path) {
    if (!_state_mutex) {
        return;
    }

    if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }

    if (path && path[0] != '\0') {
        strncpy(_current_playback_path, path, sizeof(_current_playback_path) - 1);
        _current_playback_path[sizeof(_current_playback_path) - 1] = '\0';
    } else {
        _current_playback_path[0] = '\0';
    }

    xSemaphoreGive(_state_mutex);
}

void Mp3Player::playbackTask(void* param) {
    Mp3Player* p = (Mp3Player*)param;
    ESP_LOGD(TAG, "Playback task running on core %d", xPortGetCoreID());

    PlayCommand command = {};
    while (p->_worker_running) {
        if (xQueueReceive(p->_command_queue, &command, pdMS_TO_TICKS(250)) != pdTRUE) {
            if (!p->_is_playing &&
                !p->_worker_busy &&
                !p->_playback_pending &&
                (millis() - p->_last_activity_ms) >= Mp3Player::BUFFER_RELEASE_IDLE_MS) {
                p->releaseIdleResources();
            }
            continue;
        }

        if (!p->_worker_running) {
            break;
        }

        if (command.path_count == 0 || command.paths[0][0] == '\0') {
            continue;
        }

        p->_worker_busy = true;
        p->_playback_pending = false;
        p->_stop_requested = false;
        p->_last_activity_ms = millis();

        if (!p->ensureDecoderBuffers() || !p->ensurePcmBuffer()) {
            p->_worker_busy = false;
            continue;
        }

        bool codec_locked = xSemaphoreTake(p->_codec_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
        if (!codec_locked) {
            ESP_LOGE(TAG, "❌ Failed to acquire codec mutex");
            p->clearPlaybackData();
            p->_worker_busy = false;
            continue;
        }

        bool was_enabled = p->_codec->isOutputEnabled();
        if (!was_enabled) {
            ESP_LOGD(TAG, "Enabling codec output");
            p->_codec->enableOutput(true);
        }
        xSemaphoreGive(p->_codec_mutex);

        p->_is_playing = true;
        p->updateCurrentPlaybackPath(nullptr);
        ESP_LOGD(TAG, "Starting MP3 decode loop");

        for (uint8_t path_index = 0;
             path_index < command.path_count && !p->_stop_requested && p->_worker_running;
             ++path_index) {
            const char* current_path = command.paths[path_index];
            if (current_path[0] == '\0') {
                continue;
            }

            uint8_t repeat_count = (command.path_count == 1 && command.repeat_count > 0)
                ? command.repeat_count
                : 1;
            if (!p->loadPlaybackData(current_path, repeat_count)) {
                ESP_LOGW(TAG, "Failed to load playback item: %s", current_path);
                p->updateCurrentPlaybackPath(nullptr);
                break;
            }
            p->updateCurrentPlaybackPath(current_path);

            for (uint8_t play_index = 0;
                 play_index < repeat_count && !p->_stop_requested && p->_worker_running;
                 ++play_index) {
                uint8_t* read_ptr = p->_playback_data.mp3_buffer;
                int32_t bytes_left = p->_playback_data.mp3_size;

                while (bytes_left > 0 && !p->_stop_requested && p->_worker_running) {
                    int offset = MP3FindSyncWord(read_ptr, bytes_left);
                    if (offset < 0) {
                        break;
                    }

                    read_ptr += offset;
                    bytes_left -= offset;

                    int32_t bytes_before = bytes_left;
                    int err = MP3Decode(read_ptr, &bytes_left, p->_pcm_buffer, 0);
                    int32_t bytes_consumed = bytes_before - bytes_left;

                    if (err) {
                        if (err == ERR_MP3_INDATA_UNDERFLOW) {
                            break;
                        }
                        read_ptr++;
                        bytes_left--;
                        continue;
                    }

                    read_ptr += bytes_consumed;

                    int samples = MP3GetOutputSamps();
                    int channels = MP3GetChannels();
                    int frame_sample_rate = MP3GetSampRate();

                    if (samples > 0 && channels > 0) {
                        int mono_samples = samples / channels;

                        if (channels == 2) {
                            for (int i = 0; i < mono_samples; i++) {
                                p->_pcm_buffer[i] =
                                    (p->_pcm_buffer[i * 2] + p->_pcm_buffer[i * 2 + 1]) / 2;
                            }
                        }

                        const int output_sample_rate = p->_codec->getOutputSampleRate();
                        const int16_t* output_buffer = p->_pcm_buffer;
                        int output_samples = mono_samples;
                        if (frame_sample_rate > 0 &&
                            output_sample_rate > 0 &&
                            frame_sample_rate != output_sample_rate) {
                            output_samples = calculateResampledSamples(
                                mono_samples, frame_sample_rate, output_sample_rate);
                            if (output_samples > p->_resample_buffer_capacity) {
                                if (p->_resample_buffer) {
                                    if (p->_resample_buffer_in_psram) {
                                        heap_caps_free(p->_resample_buffer);
                                    } else {
                                        free(p->_resample_buffer);
                                    }
                                    p->_resample_buffer = nullptr;
                                }

                                p->_resample_buffer = static_cast<int16_t*>(
                                    heap_caps_malloc(output_samples * sizeof(int16_t),
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                                p->_resample_buffer_in_psram = (p->_resample_buffer != nullptr);
                                if (!p->_resample_buffer) {
                                    p->_resample_buffer = static_cast<int16_t*>(
                                        malloc(output_samples * sizeof(int16_t)));
                                    p->_resample_buffer_in_psram = false;
                                }
                                if (!p->_resample_buffer) {
                                    ESP_LOGE(TAG,
                                             "❌ Failed to allocate MP3 resample buffer (%d samples)",
                                             output_samples);
                                    p->_stop_requested = true;
                                    break;
                                }
                                p->_resample_buffer_capacity = output_samples;
                            }

                            resamplePcmLinear(p->_pcm_buffer,
                                              mono_samples,
                                              frame_sample_rate,
                                              p->_resample_buffer,
                                              output_samples,
                                              output_sample_rate);
                            output_buffer = p->_resample_buffer;
                        }

                        if (xSemaphoreTake(p->_codec_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            p->_codec->write(output_buffer, output_samples);
                            xSemaphoreGive(p->_codec_mutex);
                        }
                    }

                    vTaskDelay(1);
                }
            }

            p->clearPlaybackData();
        }

        ESP_LOGD(TAG, "Playback complete, cleaning up");

        if (xSemaphoreTake(p->_codec_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (!was_enabled) {
                ESP_LOGD(TAG, "Disabling codec output");
                p->_codec->enableOutput(false);
            }
            xSemaphoreGive(p->_codec_mutex);
        }

        p->clearPlaybackData();
        p->updateCurrentPlaybackPath(nullptr);
        p->_is_playing = false;
        p->_stop_requested = false;
        p->_worker_busy = false;
        p->_last_activity_ms = millis();
    }

    p->clearPlaybackData();
    p->updateCurrentPlaybackPath(nullptr);
    p->releaseIdleResources();
    p->_is_playing = false;
    p->_stop_requested = false;
    p->_playback_pending = false;
    p->_worker_busy = false;
    ESP_LOGD(TAG, "Playback task exiting");
    RuntimeTaskRegistry::instance().markTaskStopped("mp3_play");
    vTaskDelete(nullptr);
}

void Mp3Player::stop() {
    _playback_pending = false;
    if (_command_queue) {
        xQueueReset(_command_queue);
    }
    updateCurrentPlaybackPath(nullptr);
    if (_worker_busy || _is_playing) {
        _stop_requested = true;
    }
}
