/**
 * @file GifPlayer.h
 * @brief Header for AnimatedGIF playback functionality
 *
 * Provides functions for loading, initializing, and playing animated GIF files
 * from the filesystem using the AnimatedGIF library (bitbank2).
 */

#pragma once

#include <AnimatedGIF.h>
#include <Arduino.h>
#include <FS.h>

// Forward declaration
/**
 * @brief ArduinoSSD1351.
 */
class ArduinoSSD1351;

/**
 * @brief Context structure for GIF playback
 */
struct GIFContext {
    uint8_t* sharedFrameBuffer;
    int offsetX;
    int offsetY;
};

/**
 * @brief GifPlayer.
 */
class GifPlayer {
public:
    enum class TerminalReason : uint8_t {
        NONE = 0,
        COMPLETED,
        INTERRUPTED,
        STOPPED,
        ERROR,
    };

    GifPlayer(ArduinoSSD1351* display);
    ~GifPlayer();

    /**
     * @brief Initialize the GIF player
     * @return true if initialization was successful
     */
    bool begin();

    /**
     * @brief Load a GIF file for playback
     * @param filename Path to the GIF file
     * @return true if GIF was loaded successfully
     */
    bool loadGIF(const char* filename);

    /**
     * @brief Start playing the GIF (non-blocking)
     * @param filename Path to the GIF file to play
     * @return true if playback started successfully
     */
    bool playGIF(const char* filename);

    /**
     * @brief Update GIF playback (process one frame if due)
     * @return true if a frame was drawn, false otherwise
     */
    bool update();

    /**
     * @brief Play a single frame of the current GIF
     * @param bSync Whether to synchronize with the GIF timing
     * @param delayMilliseconds Pointer to store delay until next frame
     * @return Status code (0 = success, 1 = finished, negative = error)
     */
    int playFrame(bool bSync, int* delayMilliseconds);

    /**
     * @brief Stop GIF playback and free resources
     */
    void stop();

    /**
     * @brief Check if the GIF player is initialized
     * @return true if the GIF player is initialized
     */
    bool isInitialized() const { return _initialized; }

    /**
     * @brief Check if a GIF is currently playing
     * @return true if playing, false otherwise
     */
    bool isPlaying() const { return _is_playing; }
    const String& currentGif() const { return _current_gif; }
    bool isLooping() const { return _should_loop; }

    /**
     * @brief Get the AnimatedGIF instance
     * @return Reference to the AnimatedGIF instance
     */
    AnimatedGIF& getGIF() { return _gif; }

    /**
     * @brief Get the GIF context
     * @return Reference to the GIF context
     */
    GIFContext& getContext() { return _context; }

    /**
     * @brief Request to play a specific GIF file
     * @param filename Path to the GIF file
     * @param loop Whether to loop the GIF (default: true)
     */
    uint32_t requestGIF(const char* filename, bool loop = true);

    /**
     * @brief Stop playback and clear current GIF state
     */
    void stopPlayback();

    /**
     * @brief Check if current GIF has completed one playthrough
     * @return true if GIF finished playing once
     */
    bool hasFinishedOnce() const { return _finished_once; }

    /**
     * @brief Check and clear the "finished once" flag
     * @return true if GIF finished playing once since last check
     */
    bool takeFinishedOnce();
    bool hasRequestTerminated(uint32_t request_id, TerminalReason* reason = nullptr);
    bool isRequestPendingOrActive(uint32_t request_id);
    static const char* terminalReasonName(TerminalReason reason);

    /**
     * @brief Render the first frame of the current GIF and pause playback
     * @return true if the frame was rendered, false otherwise
     */
    bool renderFirstFrame();

    /**
     * @brief Resume playback of the current GIF
     */
    void resumePlayback();

    /**
     * @brief Set the mutex used to protect display access
     * @param mutex Handle to the mutex
     */
    void setDisplayMutex(SemaphoreHandle_t mutex) {
        _display_mutex = mutex;
    }

private:
    // GIF playback task (runs on core 1)
    static void gifTaskImpl(void* parameter);
    /**
     * @brief Gif task
     */
    void gifTask();
    bool drawCurrentFrame();
    AnimatedGIF _gif;
    GIFContext _context;
    ArduinoSSD1351* _display;
    bool _initialized;

    // Task management
    uint16_t* _dmaBuffer; // Buffer for DMA transfers (one scanline)
    bool _use_dma;        // Flag to enable/disable DMA transfers
    SemaphoreHandle_t _gif_mutex; // Protects GIF state
    SemaphoreHandle_t _display_mutex; // Protects display access (external)
    
    String _requested_gif;
    String _current_gif;
    uint32_t _next_request_id;
    uint32_t _requested_request_id;
    uint32_t _active_request_id;
    uint32_t _last_terminal_request_id;
    TerminalReason _last_terminal_reason;
    volatile bool _task_running;
    bool _should_loop;
    bool _requested_loop;
    volatile bool _finished_once;

    // Playback state
    unsigned long _next_frame_time_ms;
    int _frame_delay_ms;
    bool _is_playing;
    unsigned long _last_error_restart_ms;

    // Static file handle for callbacks
    static File _gifFile;

    // Static callback functions for AnimatedGIF library
    static void* openFile(const char* fname, int32_t* pSize);
    static void closeFile(void* pHandle);
    static int32_t readFile(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen);
    static int32_t seekFile(GIFFILE* pFile, int32_t iPosition);
    static void drawCallback(GIFDRAW* pDraw);
    void markRequestTerminalLocked(uint32_t request_id, TerminalReason reason);
    void markActiveRequestTerminalLocked(TerminalReason reason);

    // Need static pointer to GifPlayer instance for callbacks
    static GifPlayer* _instance;
};

// Constants
#define GIF_WIDTH 128
#define GIF_HEIGHT 128
