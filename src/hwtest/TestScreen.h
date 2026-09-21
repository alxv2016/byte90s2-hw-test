/**
 * TestScreen.h
 *
 * Fixed text layout for hardware test pages on the 128x128 SSD1351.
 *
 *   y   0  header bar      "03/10 SOUND"
 *   y  14  status line     yellow "Test started" / green "Done" / red "FAIL"
 *   y  26  body rows 0-8   result text, 10 px pitch
 *   y 118  footer          button hint; scrolls when wider than the screen
 *
 * Body rows are either free text (setLine) or a field (setField): a white
 * "Label:" on the left and the value right-aligned to the screen edge in the
 * status color. Use fields for any label/value data so columns line up.
 *
 * Rows and the footer are composed in an off-screen strip and pushed in one
 * write, and a row whose content has not changed is not redrawn, so live
 * values and countdowns can be updated every loop without flicker. The screen
 * remembers what every row shows, so repaint() can restore the page after an
 * overlay (the power menu) has drawn over it.
 */

#pragma once

#include <Arduino.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Adafruit_GFX;
class Adafruit_SSD1351;
class ArduinoSSD1351;
class GFXcanvas16;

class TestScreen {
public:
    enum class Status : uint8_t {
        STARTED,
        DONE,
        FAILED,
    };

    static constexpr uint8_t BODY_ROWS = 9;
    static constexpr uint8_t MAX_CHARS = 21;

    TestScreen();
    ~TestScreen();

    void begin(ArduinoSSD1351* display, SemaphoreHandle_t display_mutex);

    /**
     * @brief Clear the screen and draw the header plus a yellow "Test started"
     */
    void showTest(uint8_t number, uint8_t total, const char* name);

    /**
     * @brief Redraw header and status after a test drew over the whole screen
     */
    void redrawFrame();

    void setStatus(Status status);

    /**
     * @brief Draw one body row of free text and log it to serial
     */
    void setLine(uint8_t row, uint16_t color, const char* format, ...)
        __attribute__((format(printf, 4, 5)));

    /**
     * @brief Draw one body row of free text; for live values
     *
     * Logged at VERBOSE, and only when the text changes, so live rows do not
     * flood the console. Build with CORE_DEBUG_LEVEL=5 to see them.
     */
    void updateLine(uint8_t row, uint16_t color, const char* format, ...)
        __attribute__((format(printf, 4, 5)));

    /**
     * @brief Draw "Label:" left-aligned and the value right-aligned, and log it
     *
     * Label and value share MAX_CHARS; the value is clipped if both do not fit.
     *
     * @param color Value color; the label is always white
     */
    void setField(uint8_t row, const char* label, uint16_t color, const char* format, ...)
        __attribute__((format(printf, 5, 6)));

    /**
     * @brief setField() for live values; logged at VERBOSE, only on change
     */
    void updateField(uint8_t row, const char* label, uint16_t color, const char* format, ...)
        __attribute__((format(printf, 5, 6)));

    /**
     * @brief Draw a horizontal level bar filling one body row
     */
    void drawBar(uint8_t row, uint8_t percent, uint16_t color);

    void clearBody();

    /**
     * @brief Show a footer hint; text wider than the screen scrolls as a marquee
     *
     * A scrolling footer only moves while updateFooter() is being called.
     */
    void setFooter(const char* text, uint16_t color);

    /**
     * @brief Advance a scrolling footer; call every loop, cheap when idle
     */
    void updateFooter();

    /**
     * @brief Redraw the whole page (header, status, rows, footer) from memory
     *
     * Restores the page after something else drew over it.
     */
    void repaint();

    /**
     * @brief Take the display for free-form drawing; pair with unlockGfx()
     * @return null if the display is missing or busy
     */
    Adafruit_SSD1351* lockGfx();
    void unlockGfx();

    const char* getTestName() const { return _name; }

private:
    static constexpr int16_t HEADER_HEIGHT = 12;
    static constexpr int16_t STATUS_Y = 14;
    static constexpr int16_t BODY_Y = 26;
    static constexpr int16_t ROW_PITCH = 10;
    static constexpr int16_t FOOTER_Y = 118;
    static constexpr int16_t FOOTER_HEIGHT = 10;
    static constexpr int16_t STRIP_HEIGHT = 10;  // one body row or the footer
    static constexpr uint32_t FOOTER_STEP_MS = 30;  // 1 px per step, ~33 px/s
    static constexpr int16_t CHAR_WIDTH = 6;  // size-1 glyph plus spacing

    void drawHeader();
    void drawStatus();
    // What one body row shows. Kept so unchanged updates are skipped and the
    // page can be repainted.
    struct RowRecord {
        enum class Kind : uint8_t {
            EMPTY,
            TEXT,
            FIELD,
            BAR,
        };
        Kind kind;
        uint16_t color;
        uint8_t percent;          // BAR
        char text[MAX_CHARS + 1];  // TEXT: the line; FIELD: "Label:"
        char value[MAX_CHARS + 1]; // FIELD: the right-aligned value
    };

    bool drawRow(uint8_t row, uint16_t color, const char* text);
    bool drawField(uint8_t row, const char* label, uint16_t color, const char* value);

    /**
     * @brief Store record for row and draw it, unless the row already shows it
     * @return true if the row changed
     */
    bool updateRow(uint8_t row, const RowRecord& record);
    void renderRow(uint8_t row);
    void drawFooter();

    /**
     * @brief Compose one full-width strip at y and push it to the panel
     *
     * draw() renders into the given target at the given top y. With the strip
     * canvas the target is off-screen (top 0); without it, draw() renders
     * straight to the panel after the strip is cleared.
     */
    void drawStrip(int16_t y, const std::function<void(Adafruit_GFX&, int16_t)>& draw);

    void forgetRows();

    ArduinoSSD1351* _display;
    SemaphoreHandle_t _display_mutex;
    uint8_t _number;
    uint8_t _total;
    const char* _name;
    Status _status;

    char _footer_text[64];
    uint16_t _footer_color;
    bool _footer_scrolling;
    int16_t _footer_offset_px;
    int16_t _footer_loop_px;
    uint32_t _footer_last_step_ms;
    GFXcanvas16* _strip_canvas;       // shared off-screen strip; null falls back to direct draw
    RowRecord _rows[BODY_ROWS];
};
