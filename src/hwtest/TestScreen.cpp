/**
 * TestScreen.cpp
 *
 * Implementation for TestScreen.
 */

#include "hwtest/TestScreen.h"

#include "ArduinoSSD1351.h"
#include "DeviceConfig.h"

#include <Adafruit_GFX.h>

#include <esp_log.h>
#include <stdarg.h>
#include <string.h>

namespace {
static const char* TAG = "TestScreen";
constexpr uint16_t COLOR_HEADER_BG = 0x18C3;  // dark grey
// Spacer between repeats of a scrolling footer.
constexpr const char* FOOTER_GAP = "   |   ";

}  // namespace

TestScreen::TestScreen()
    : _display(nullptr)
    , _display_mutex(nullptr)
    , _number(0)
    , _total(0)
    , _name("")
    , _status(Status::STARTED)
    , _footer_text{}
    , _footer_color(COLOR_WHITE)
    , _footer_scrolling(false)
    , _footer_offset_px(0)
    , _footer_loop_px(0)
    , _footer_last_step_ms(0)
    , _strip_canvas(nullptr)
    , _rows{} {
    static_assert(ROW_PITCH == STRIP_HEIGHT && FOOTER_HEIGHT == STRIP_HEIGHT,
                  "rows and footer share one strip canvas");
}

TestScreen::~TestScreen() {
    delete _strip_canvas;
}

void TestScreen::begin(ArduinoSSD1351* display, SemaphoreHandle_t display_mutex) {
    _display = display;
    _display_mutex = display_mutex;

    _strip_canvas = new GFXcanvas16(DISPLAY_WIDTH, STRIP_HEIGHT);
    if (!_strip_canvas->getBuffer()) {
        ESP_LOGW(TAG, "No strip buffer; rows will draw directly and may flicker");
        delete _strip_canvas;
        _strip_canvas = nullptr;
    }
}

void TestScreen::showTest(uint8_t number, uint8_t total, const char* name) {
    _number = number;
    _total = total;
    _name = name ? name : "";
    _status = Status::STARTED;
    // A new page starts without a footer; the runner sets one when it ends.
    _footer_text[0] = '\0';
    _footer_scrolling = false;
    redrawFrame();
    ESP_LOGI(TAG, "==== [%u/%u] %s: Test started ====", _number, _total, _name);
}

void TestScreen::redrawFrame() {
    Adafruit_SSD1351* gfx = lockGfx();
    if (!gfx) {
        return;
    }
    gfx->fillScreen(COLOR_BLACK);
    unlockGfx();
    forgetRows();

    drawHeader();
    drawStatus();
}

void TestScreen::repaint() {
    Adafruit_SSD1351* gfx = lockGfx();
    if (!gfx) {
        return;
    }
    gfx->fillScreen(COLOR_BLACK);
    unlockGfx();

    drawHeader();
    drawStatus();
    for (uint8_t row = 0; row < BODY_ROWS; row++) {
        renderRow(row);
    }
    drawFooter();
}

void TestScreen::setStatus(Status status) {
    _status = status;
    drawStatus();

    if (status == Status::DONE) {
        ESP_LOGI(TAG, "==== [%u/%u] %s: Done ====", _number, _total, _name);
    } else if (status == Status::FAILED) {
        ESP_LOGW(TAG, "==== [%u/%u] %s: FAIL ====", _number, _total, _name);
    }
}

void TestScreen::setLine(uint8_t row, uint16_t color, const char* format, ...) {
    char text[64];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    ESP_LOGI(TAG, "  %s", text);
    drawRow(row, color, text);
}

void TestScreen::updateLine(uint8_t row, uint16_t color, const char* format, ...) {
    char text[64];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    if (drawRow(row, color, text)) {
        ESP_LOGV(TAG, "  %s", text);
    }
}

void TestScreen::setField(uint8_t row, const char* label, uint16_t color,
                          const char* format, ...) {
    char value[64];
    va_list args;
    va_start(args, format);
    vsnprintf(value, sizeof(value), format, args);
    va_end(args);

    ESP_LOGI(TAG, "  %s: %s", label ? label : "", value);
    drawField(row, label, color, value);
}

void TestScreen::updateField(uint8_t row, const char* label, uint16_t color,
                             const char* format, ...) {
    char value[64];
    va_list args;
    va_start(args, format);
    vsnprintf(value, sizeof(value), format, args);
    va_end(args);

    if (drawField(row, label, color, value)) {
        ESP_LOGV(TAG, "  %s: %s", label ? label : "", value);
    }
}

void TestScreen::drawBar(uint8_t row, uint8_t percent, uint16_t color) {
    RowRecord record = {};
    record.kind = RowRecord::Kind::BAR;
    record.color = color;
    record.percent = min<uint8_t>(percent, 100);
    updateRow(row, record);
}

void TestScreen::clearBody() {
    Adafruit_SSD1351* gfx = lockGfx();
    if (!gfx) {
        return;
    }
    gfx->fillRect(0, BODY_Y, DISPLAY_WIDTH, DISPLAY_HEIGHT - BODY_Y, COLOR_BLACK);
    unlockGfx();
    forgetRows();
}

void TestScreen::setFooter(const char* text, uint16_t color) {
    snprintf(_footer_text, sizeof(_footer_text), "%s", text ? text : "");
    _footer_color = color;
    _footer_offset_px = 0;
    _footer_last_step_ms = millis();

    size_t text_chars = strlen(_footer_text);
    _footer_scrolling = static_cast<int16_t>(text_chars * CHAR_WIDTH) > DISPLAY_WIDTH - 4;
    _footer_loop_px = static_cast<int16_t>((text_chars + strlen(FOOTER_GAP)) * CHAR_WIDTH);

    if (_footer_scrolling && !_strip_canvas) {
        _footer_scrolling = false;  // no buffer: fall back to a clipped static line
    }
    drawFooter();
}

void TestScreen::updateFooter() {
    if (!_footer_scrolling) {
        return;
    }
    uint32_t now = millis();
    if (now - _footer_last_step_ms < FOOTER_STEP_MS) {
        return;
    }
    _footer_last_step_ms = now;
    _footer_offset_px = (_footer_offset_px + 1) % _footer_loop_px;
    drawFooter();
}

void TestScreen::drawFooter() {
    drawStrip(FOOTER_Y, [&](Adafruit_GFX& target, int16_t top) {
        target.setTextSize(1);
        target.setTextWrap(false);
        target.setTextColor(_footer_color);
        if (!_footer_scrolling) {
            target.setCursor(2, top + 1);
            target.print(_footer_text);
            return;
        }
        // Text + gap repeated across the strip, shifted left by the offset.
        for (int16_t x = -_footer_offset_px; x < DISPLAY_WIDTH; x += _footer_loop_px) {
            target.setCursor(x, top + 1);
            target.print(_footer_text);
            target.print(FOOTER_GAP);
        }
    });
}

void TestScreen::drawStrip(int16_t y,
                           const std::function<void(Adafruit_GFX&, int16_t)>& draw) {
    if (_strip_canvas) {
        _strip_canvas->fillScreen(COLOR_BLACK);
        draw(*_strip_canvas, 0);
        Adafruit_SSD1351* gfx = lockGfx();
        if (!gfx) {
            return;
        }
        gfx->drawRGBBitmap(0, y, _strip_canvas->getBuffer(), DISPLAY_WIDTH, STRIP_HEIGHT);
        unlockGfx();
        return;
    }

    Adafruit_SSD1351* gfx = lockGfx();
    if (!gfx) {
        return;
    }
    gfx->fillRect(0, y, DISPLAY_WIDTH, STRIP_HEIGHT, COLOR_BLACK);
    draw(*gfx, y);
    unlockGfx();
}

bool TestScreen::updateRow(uint8_t row, const RowRecord& record) {
    if (row >= BODY_ROWS) {
        return false;
    }
    RowRecord& current = _rows[row];
    bool unchanged = current.kind == record.kind && current.color == record.color &&
                     current.percent == record.percent &&
                     strcmp(current.text, record.text) == 0 &&
                     strcmp(current.value, record.value) == 0;
    if (unchanged) {
        return false;
    }
    current = record;
    renderRow(row);
    return true;
}

void TestScreen::renderRow(uint8_t row) {
    const RowRecord& record = _rows[row];
    if (record.kind == RowRecord::Kind::EMPTY) {
        return;
    }

    drawStrip(BODY_Y + row * ROW_PITCH, [&](Adafruit_GFX& target, int16_t top) {
        if (record.kind == RowRecord::Kind::BAR) {
            int16_t width = DISPLAY_WIDTH - 4;
            int16_t fill = (width - 2) * record.percent / 100;
            target.drawRect(2, top, width, ROW_PITCH - 2, COLOR_WHITE);
            target.fillRect(3, top + 1, fill, ROW_PITCH - 4, record.color);
            return;
        }

        target.setTextSize(1);
        target.setTextWrap(false);
        target.setCursor(2, top + 1);
        if (record.kind == RowRecord::Kind::TEXT) {
            target.setTextColor(record.color);
            target.print(record.text);
            return;
        }

        target.setTextColor(COLOR_WHITE);
        target.print(record.text);
        // Each glyph is 6 px including its trailing blank column, so ending the
        // last glyph at the panel edge leaves a 1 px right margin.
        int16_t value_px = static_cast<int16_t>(strlen(record.value) * CHAR_WIDTH);
        target.setTextColor(record.color);
        target.setCursor(DISPLAY_WIDTH - value_px, top + 1);
        target.print(record.value);
    });
}

void TestScreen::forgetRows() {
    for (uint8_t i = 0; i < BODY_ROWS; i++) {
        _rows[i] = RowRecord{};
    }
}

Adafruit_SSD1351* TestScreen::lockGfx() {
    if (!_display) {
        return nullptr;
    }
    if (_display_mutex &&
        xSemaphoreTake(_display_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return nullptr;
    }
    return _display->getAdafruitDisplay();
}

void TestScreen::unlockGfx() {
    if (_display_mutex) {
        xSemaphoreGive(_display_mutex);
    }
}

void TestScreen::drawHeader() {
    Adafruit_SSD1351* gfx = lockGfx();
    if (!gfx) {
        return;
    }

    char title[MAX_CHARS + 1];
    snprintf(title, sizeof(title), "%02u/%02u %s", _number, _total, _name);

    gfx->fillRect(0, 0, DISPLAY_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    gfx->setTextSize(1);
    gfx->setTextWrap(false);
    gfx->setTextColor(COLOR_WHITE);
    gfx->setCursor(2, 2);
    gfx->print(title);
    unlockGfx();
}

void TestScreen::drawStatus() {
    Adafruit_SSD1351* gfx = lockGfx();
    if (!gfx) {
        return;
    }

    const char* text = "Test started";
    uint16_t color = COLOR_YELLOW;
    if (_status == Status::DONE) {
        text = "Done";
        color = COLOR_GREEN;
    } else if (_status == Status::FAILED) {
        text = "FAIL";
        color = COLOR_RED;
    }

    gfx->fillRect(0, STATUS_Y, DISPLAY_WIDTH, 9, COLOR_BLACK);
    gfx->setTextSize(1);
    gfx->setTextWrap(false);
    gfx->setTextColor(color);
    gfx->setCursor(2, STATUS_Y);
    gfx->print(text);
    unlockGfx();
}

bool TestScreen::drawField(uint8_t row, const char* label, uint16_t color,
                           const char* value) {
    RowRecord record = {};
    record.kind = RowRecord::Kind::FIELD;
    record.color = color;
    // "Label:" takes its length + 1; the value gets whatever is left.
    snprintf(record.text, sizeof(record.text), "%.*s:", MAX_CHARS - 2, label ? label : "");
    snprintf(record.value, sizeof(record.value), "%.*s",
             static_cast<int>(MAX_CHARS - strlen(record.text)), value ? value : "");
    return updateRow(row, record);
}

bool TestScreen::drawRow(uint8_t row, uint16_t color, const char* text) {
    RowRecord record = {};
    record.kind = RowRecord::Kind::TEXT;
    record.color = color;
    snprintf(record.text, sizeof(record.text), "%s", text ? text : "");
    return updateRow(row, record);
}
