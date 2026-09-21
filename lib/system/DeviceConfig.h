/**
 * DeviceConfig.h
 *
 * Hardware configuration and PIN definitions for Byte90-Xiaozhi ESP32-S3 device.
 * Centralizes all hardware-specific settings for easy maintenance and board variants.
 *
 * Board: ESP32-S3 (Seeed Studio XIAO ESP32S3)
 * Author: Byte90 Team
 */

#pragma once

#include <Arduino.h>
// ========================================================================
// DISPLAY PINS (SSD1351 OLED - SPI)
// ========================================================================
#define DISPLAY_SPI_SCK_PIN    D8
#define DISPLAY_SPI_MOSI_PIN   D10
#define DISPLAY_SPI_CS_PIN     D7
#define DISPLAY_DC_PIN         D6
#define DISPLAY_RESET_PIN      -1 // Reset pin bypass through hardware display initializer chip

#define DISPLAY_WIDTH         128
#define DISPLAY_HEIGHT        128

// ========================================================================
// I2C PINS (AXP2101 Power Management)
// ========================================================================
#define I2C_SDA_PIN            D4
#define I2C_SCL_PIN            D5
#define AXP2101_I2C_ADDR       0x34
#define AXP2101_IRQ_PIN        D9

// ========================================================================
// AUDIO PINS
// ========================================================================
// Speaker (MAX98357 I2S)
#define AUDIO_SPEAKER_BCLK     D3
#define AUDIO_SPEAKER_LRC      D1
#define AUDIO_SPEAKER_DOUT     D2

// I2S Microphone (ICS-43434) - Full-duplex with speaker
#define AUDIO_MIC_I2S_BCLK     AUDIO_SPEAKER_BCLK  // Shared
#define AUDIO_MIC_I2S_LRC      AUDIO_SPEAKER_LRC   // Shared
#define AUDIO_MIC_I2S_DATA     D0                  // Unique

// ========================================================================
// AUDIO CONFIGURATION
// ========================================================================
#define AUDIO_INPUT_SAMPLE_RATE 16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000
#define AUDIO_SAMPLE_RATE      16000

// ========================================================================
// NTP SERVERS
// ========================================================================
#define NTP_SERVER_DEFAULT    "time.windows.com"

// ========================================================================
// WIFI TEST CREDENTIALS
// ========================================================================
// When WIFI_TEST_SSID is non-empty, the WiFi test joins this network instead
// of credentials saved in NVS (serial: wifi <ssid> <password>). Leave the
// password empty for an open network.
//
// Do not commit real credentials. To keep them out of git, leave these empty
// and pass them through the environment when building instead:
//   export PLATFORMIO_BUILD_FLAGS='-DWIFI_TEST_SSID=\"MyNetwork\" -DWIFI_TEST_PASSWORD=\"secret\"'
#ifndef WIFI_TEST_SSID
#define WIFI_TEST_SSID         ""
#endif
#ifndef WIFI_TEST_PASSWORD
#define WIFI_TEST_PASSWORD     ""
#endif
