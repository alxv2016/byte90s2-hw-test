/**
 * HwTestContext.h
 *
 * Hardware handles shared by every hardware test. Any pointer may be null
 * when its driver failed to initialize; tests report that as a failure.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class ArduinoSSD1351;
class AXP2101;
class HapticsDriver;
class Adxl345;
class ClockRtc;
class AudioCodec;
class ToneGenerator;
class Mp3Player;
class LittleFsAdapter;
class GifPlayer;

struct HwTestContext {
    ArduinoSSD1351* display = nullptr;
    SemaphoreHandle_t display_mutex = nullptr;
    AXP2101* power = nullptr;
    HapticsDriver* haptics = nullptr;
    Adxl345* imu = nullptr;
    ClockRtc* rtc = nullptr;
    AudioCodec* codec = nullptr;
    ToneGenerator* tone = nullptr;
    Mp3Player* mp3 = nullptr;
    LittleFsAdapter* filesystem = nullptr;
    GifPlayer* gif = nullptr;
};
