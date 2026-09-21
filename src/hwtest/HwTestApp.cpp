/**
 * HwTestApp.cpp
 *
 * Implementation for HwTestApp.
 */

#include "hwtest/HwTestApp.h"

#include "Adxl345.h"
#include "ArduinoSSD1351.h"
#include "AudioCodec.h"
#include "Axp2101.h"
#include "ClockRtc.h"
#include "DeviceConfig.h"
#include "DosBootAnimator.h"
#include "GifPlayer.h"
#include "HapticsDriver.h"
#include "LittleFsAdapter.h"
#include "Mp3Player.h"
#include "RuntimeDiagnostics.h"
#include "RuntimeTaskRegistry.h"
#include "SharedI2cBus.h"
#include "StartupImage.h"
#include "ToneGenerator.h"
#include "hwtest/HwTestRunner.h"

#include <esp_log.h>

namespace {
static const char* TAG = "HwTestApp";
constexpr uint32_t SPLASH_HOLD_MS = 600;

// Drop a driver whose begin() failed so tests see null and report it.
template <typename T>
T* keepIfReady(T* driver, bool ready, const char* name) {
    if (ready) {
        ESP_LOGI(TAG, "%s ready", name);
        return driver;
    }
    ESP_LOGE(TAG, "%s failed to initialize", name);
    delete driver;
    return nullptr;
}
}  // namespace

HwTestApp::HwTestApp()
    : _context()
    , _screen()
    , _runner(nullptr) {
}

HwTestApp::~HwTestApp() {
    delete _runner;
}

void HwTestApp::begin() {
    Serial.begin(115200);
    if (!RuntimeTaskRegistry::instance().begin()) {
        ESP_LOGE(TAG, "Failed to initialize RuntimeTaskRegistry");
    }
    RuntimeDiagnostics::instance().begin();

    initializeHardware();
    runBootAnimation();
    showSplash();

    _runner = new HwTestRunner(_context, _screen);
    _runner->begin();
}

void HwTestApp::loop() {
    if (_runner) {
        _runner->loop();
    }
}

void HwTestApp::initializeHardware() {
    SharedI2cBus& i2c = SharedI2cBus::getInstance();
    i2c.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    AXP2101* power = new AXP2101(&i2c, AXP2101_I2C_ADDR);
    _context.power = keepIfReady(power, power->begin(), "AXP2101");
    if (_context.power) {
        _context.power->enableTelemetry();
        // A 6 s hold opens the software power menu (HwTestRunner), so the
        // PMIC must not cut power on its own long-press timer.
        _context.power->setHardwarePowerOffEnabled(false);
    }

    HapticsDriver* haptics = new HapticsDriver(&i2c);
    _context.haptics =
        keepIfReady(haptics, haptics->begin(HapticsDriver::ACTUATOR_ERM), "DRV2605L");

    Adxl345* imu = new Adxl345();
    _context.imu = keepIfReady(imu, imu->begin(&i2c), "ADXL345");

    // Display comes up after the PMIC and I2C peripherals, matching the
    // product firmware's bring-up order.
    _context.display_mutex = xSemaphoreCreateMutex();

    ArduinoSSD1351* display =
        new ArduinoSSD1351(DISPLAY_SPI_CS_PIN, DISPLAY_DC_PIN, DISPLAY_RESET_PIN,
                           DISPLAY_SPI_SCK_PIN, DISPLAY_SPI_MOSI_PIN);
    _context.display = keepIfReady(display, display->begin(), "Display");
    if (_context.display) {
        _context.display->setBrightnessPercent(100);
        _context.display->getAdafruitDisplay()->fillScreen(COLOR_BLACK);
    }
    _screen.begin(_context.display, _context.display_mutex);

    ClockRtc* rtc = new ClockRtc();
    _context.rtc = keepIfReady(rtc, rtc->begin(i2c), "PCF8563");

    // Default volume, never muted; no saved audio settings in the tester.
    AudioCodec* codec =
        new AudioCodec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_SPEAKER_BCLK,
                       AUDIO_SPEAKER_LRC, AUDIO_SPEAKER_DOUT, AUDIO_MIC_I2S_DATA);
    _context.codec = keepIfReady(codec, codec->begin(), "Audio codec");
    if (_context.codec) {
        _context.codec->start();
        _context.tone = new ToneGenerator(_context.codec);
    }

    LittleFsAdapter* filesystem = new LittleFsAdapter();
    _context.filesystem =
        keepIfReady(filesystem, filesystem->begin() == FSStatus::FS_SUCCESS, "LittleFS");

    if (_context.codec && _context.filesystem) {
        _context.mp3 = new Mp3Player(_context.codec, _context.filesystem);
    }

    if (_context.display) {
        GifPlayer* gif = new GifPlayer(_context.display);
        gif->setDisplayMutex(_context.display_mutex);
        _context.gif = keepIfReady(gif, gif->begin(), "GIF player");
    }
}

void HwTestApp::runBootAnimation() {
    if (!_context.display) {
        return;
    }

    DosBootAnimator boot;
    boot.begin(_context.display, _context.display_mutex);
    boot.setToneGenerator(_context.tone);
    boot.setTintColor(COLOR_YELLOW, false);
    boot.runFast();
}

void HwTestApp::showSplash() {
    Adafruit_SSD1351* gfx = _screen.lockGfx();
    if (!gfx) {
        return;
    }
    gfx->drawRGBBitmap(0, 0, STARTUP_STATIC, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    _screen.unlockGfx();
    delay(SPLASH_HOLD_MS);
}
