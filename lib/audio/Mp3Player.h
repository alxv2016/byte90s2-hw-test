/**
 * Mp3Player.h
 *
 * Declarations for Mp3Player.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Project includes
#include "AudioCodec.h"

// Forward declarations
/**
 * @brief LittleFsAdapter.
 */
class LittleFsAdapter;

/**
 * Mp3Player - MP3 file playback
 *
 * Features:
 * - MP3 file playback from filesystem
 * - Async playback in a persistent FreeRTOS worker task
 * - Thread-safe codec access
 *
 * Architecture:
 * - Loads entire MP3 file to memory (48KB limit)
 * - Decodes and plays in a background worker
 * - New play requests replace older pending requests
 * - Uses semaphore for codec synchronization
 */
class Mp3Player {
public:
    // Keep sequence capacity flexible for future module-owned audio flows.
    static const size_t MAX_SEQUENCE_ITEMS = 9;

    enum class PlaybackPolicy : uint8_t {
        INTERRUPT_CURRENT = 0,
        ONLY_IF_IDLE = 1,
    };

    /**
     * @brief Construct MP3 player instance
     *
     * @param codec Pointer to audio codec
     * @param filesystem Pointer to filesystem manager
     */
    Mp3Player(AudioCodec* codec, LittleFsAdapter* filesystem);

    /**
     * @brief Destroy MP3 player and cleanup resources
     */
    ~Mp3Player();

    /**
     * @brief Play MP3 file from filesystem
     *
     * @param path File path (must be .mp3 file)
     * @param repeat_count Number of times to play the file back-to-back
     * @return true if playback was queued, false on validation/error
     */
    bool playFile(const char* path,
                  uint8_t repeat_count = 1,
                  PlaybackPolicy policy = PlaybackPolicy::INTERRUPT_CURRENT);
    bool playSequence(const char* const* paths,
                      size_t path_count,
                      PlaybackPolicy policy = PlaybackPolicy::INTERRUPT_CURRENT);
    bool preloadFile(const char* path);
    bool getCurrentPlaybackPath(char* out_path, size_t out_size) const;
    void stop();
    bool isPlaying() const { return _is_playing || _worker_busy || _playback_pending; }

private:
    /**
     * @brief Background playback task (static entry point)
     *
     * @param param Pointer to Mp3Player instance
     */
    static void playbackTask(void* param);
    bool ensureWorkerTask();
    bool ensureDecoderBuffers();
    bool ensurePcmBuffer();
    bool validatePlaybackRequest(const char* path);
    bool validateSequenceRequest(const char* const* paths, size_t path_count);
    bool loadPlaybackData(const char* path, uint8_t repeat_count);
    void clearPlaybackData();
    void updateCurrentPlaybackPath(const char* path);
    void releaseIdleResources();
    uint8_t* allocateMp3Buffer(size_t size, bool prefer_psram, bool* used_psram);
    void freeMp3Buffer(uint8_t* buffer, bool in_psram);

    struct CacheEntry {
        char path[64];
        uint8_t* buffer;
        size_t size;
        uint32_t last_used_ms;
        bool in_use;
        bool in_psram;
    };

    CacheEntry* findCacheEntry(const char* path);
    bool isPlaybackUsingCacheEntry(const CacheEntry* entry) const;
    CacheEntry* allocateCacheEntry();
    void releaseCacheEntry(CacheEntry* entry);

    // Audio output
    AudioCodec* _codec;

    // Filesystem access
    LittleFsAdapter* _filesystem;

    // FreeRTOS primitives
    SemaphoreHandle_t _codec_mutex;
    SemaphoreHandle_t _state_mutex;
    QueueHandle_t _command_queue;
    volatile bool _is_playing;
    volatile bool _stop_requested;
    volatile bool _playback_pending;
    volatile bool _worker_running;
    volatile bool _worker_busy;

    // Playback data
    static const size_t COMMAND_QUEUE_DEPTH = 1;
    static const size_t MAX_PATH_LENGTH = 64;
    static const size_t MAX_FILE_SIZE = 40960;  // 40KB limit matches current runtime assets
    static const size_t CACHE_SLOTS = 3;
    static const uint32_t BUFFER_RELEASE_IDLE_MS = 5000;
    static const uint32_t OPEN_ERROR_LOG_INTERVAL_MS = 30000;

    struct PlayCommand {
        char paths[MAX_SEQUENCE_ITEMS][MAX_PATH_LENGTH];
        uint8_t path_count;
        uint8_t repeat_count;
    };

/**
 * @brief PlaybackData.
 */
    struct PlaybackData {
        uint8_t* mp3_buffer;
        size_t mp3_size;
        const char* path;
        uint8_t repeat_count;
        bool owns_buffer;
        bool buffer_in_psram;
    } _playback_data;

    int16_t* _pcm_buffer;
    bool _pcm_buffer_in_psram;
    int16_t* _resample_buffer;
    bool _resample_buffer_in_psram;
    int _resample_buffer_capacity;
    bool _decoder_ready;
    uint32_t _last_activity_ms;
    char _last_open_error_path[MAX_PATH_LENGTH];
    uint32_t _last_open_error_ms;
    CacheEntry _cache[CACHE_SLOTS];
    char _current_playback_path[MAX_PATH_LENGTH];
};
