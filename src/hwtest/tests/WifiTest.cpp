/**
 * WifiTest.cpp
 *
 * Implementation for WifiTest.
 */

#include "hwtest/tests/WifiTest.h"

#include "DeviceConfig.h"
#include "DisplayColors.h"
#include "WifiCredentialStore.h"

#include <WiFi.h>
#include <esp_log.h>

namespace {
static const char* TAG = "WifiTest";
constexpr uint32_t SCAN_TIMEOUT_MS = 15000;
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t DRAW_INTERVAL_MS = 500;
constexpr uint8_t SHOWN_NETWORKS = 3;
}  // namespace

WifiTest::WifiTest(HwTestContext& context)
    : HardwareTest(context)
    , _phase(Phase::SCANNING)
    , _phase_started_ms(0)
    , _last_draw_ms(0)
    , _scan_ok(false) {
}

void WifiTest::start(TestScreen& screen) {
    _phase = Phase::SCANNING;
    _phase_started_ms = millis();
    _last_draw_ms = 0;
    _scan_ok = false;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    screen.setField(0, "MAC", COLOR_WHITE, "%s", WiFi.macAddress().c_str());

    // Async scan; WiFi.scanComplete() reports progress.
    int16_t status = WiFi.scanNetworks(true);
    screen.setField(1, "Scan", status == WIFI_SCAN_FAILED ? COLOR_RED : COLOR_YELLOW, "%s",
                    status == WIFI_SCAN_FAILED ? "start failed" : "scanning...");
}

HardwareTest::Result WifiTest::update(TestScreen& screen) {
    uint32_t now = millis();
    uint32_t elapsed_ms = now - _phase_started_ms;

    if (_phase == Phase::SCANNING) {
        int16_t status = WiFi.scanComplete();
        if (status == WIFI_SCAN_RUNNING && elapsed_ms < SCAN_TIMEOUT_MS) {
            return Result::RUNNING;
        }
        if (status < 0) {
            WiFi.scanDelete();
            screen.setField(1, "Scan", COLOR_RED, "%s",
                            status == WIFI_SCAN_RUNNING ? "timed out" : "failed");
            return Result::FAILED;
        }
        return showScanResults(screen, status);
    }

    if (WiFi.status() == WL_CONNECTED) {
        return finishConnect(screen, true);
    }
    if (elapsed_ms >= CONNECT_TIMEOUT_MS) {
        return finishConnect(screen, false);
    }
    if (now - _last_draw_ms >= DRAW_INTERVAL_MS) {
        _last_draw_ms = now;
        screen.updateField(7, "Connecting", COLOR_YELLOW, "%lu s",
                           static_cast<unsigned long>(elapsed_ms / 1000));
    }
    return Result::RUNNING;
}

HardwareTest::Result WifiTest::showScanResults(TestScreen& screen, int16_t count) {
    _scan_ok = count > 0;
    screen.setField(1, "Scan", _scan_ok ? COLOR_GREEN : COLOR_RED, "%d found", count);

    // The screen shows the strongest few; serial gets the full list.
    for (int16_t i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  %2d  %4d dBm  ch %2d  %-4s  %s", i + 1, static_cast<int>(WiFi.RSSI(i)),
                 static_cast<int>(WiFi.channel(i)),
                 WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "sec",
                 WiFi.SSID(i).c_str());
    }

    // Results arrive sorted strongest first.
    for (uint8_t i = 0; i < SHOWN_NETWORKS && i < count; i++) {
        // SSID as the label (clipped), signal strength right-aligned.
        char ssid[12];
        snprintf(ssid, sizeof(ssid), "%s", WiFi.SSID(i).c_str());
        screen.setField(i + 2, ssid, COLOR_WHITE, "%d dBm", static_cast<int>(WiFi.RSSI(i)));
    }
    WiFi.scanDelete();

    // Hardcoded test credentials (DeviceConfig.h) win over anything in NVS.
    String ssid = WIFI_TEST_SSID;
    String password = WIFI_TEST_PASSWORD;
    const char* source = "DeviceConfig.h";
    if (ssid.isEmpty()) {
        WifiCredentialStore::load(ssid, password);
        source = "NVS";
    }
    if (ssid.isEmpty()) {
        screen.setLine(6, COLOR_CYAN, "No saved WiFi creds");
        screen.setLine(7, COLOR_CYAN, "serial: wifi SSID PW");
        return _scan_ok ? Result::PASSED : Result::FAILED;
    }

    screen.setField(6, "Join", COLOR_WHITE, "%s", ssid.c_str());
    ESP_LOGI(TAG, "Joining '%s' from %s (timeout %lu ms)", ssid.c_str(), source,
             static_cast<unsigned long>(CONNECT_TIMEOUT_MS));
    WiFi.begin(ssid.c_str(), password.c_str());
    _phase = Phase::CONNECTING;
    _phase_started_ms = millis();
    return Result::RUNNING;
}

HardwareTest::Result WifiTest::finishConnect(TestScreen& screen, bool connected) {
    ESP_LOGI(TAG, "Join %s after %lu ms (status %d)", connected ? "succeeded" : "failed",
             static_cast<unsigned long>(millis() - _phase_started_ms),
             static_cast<int>(WiFi.status()));
    if (!connected) {
        WiFi.disconnect(false);
        screen.setField(7, "Connect", COLOR_RED, "failed (%d)", static_cast<int>(WiFi.status()));
        return Result::FAILED;
    }

    screen.setField(7, "IP", COLOR_GREEN, "%s", WiFi.localIP().toString().c_str());
    screen.setField(8, "RSSI", COLOR_GREEN, "%d dBm", static_cast<int>(WiFi.RSSI()));
    // UTC; the RTC test waits for the first sync before writing the RTC.
    configTime(0, 0, NTP_SERVER_DEFAULT, "pool.ntp.org");
    return _scan_ok ? Result::PASSED : Result::FAILED;
}
