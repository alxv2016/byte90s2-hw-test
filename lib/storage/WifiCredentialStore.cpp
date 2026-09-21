/**
 * WifiCredentialStore.cpp
 *
 * Implementation for WifiCredentialStore.
 */

#include "WifiCredentialStore.h"

#include <Preferences.h>
#include <esp_log.h>

namespace {
static const char* TAG = "WifiCredentialStore";
constexpr const char* NAMESPACE = "wifi";
constexpr const char* KEY_SSID = "ssid";
constexpr const char* KEY_PASSWORD = "password";
}  // namespace

bool WifiCredentialStore::load(String& ssid, String& password) {
    Preferences prefs;
    // Read-only open fails when the namespace has never been written.
    if (!prefs.begin(NAMESPACE, true)) {
        ssid = "";
        password = "";
        return false;
    }
    ssid = prefs.getString(KEY_SSID, "");
    password = prefs.getString(KEY_PASSWORD, "");
    prefs.end();
    return ssid.length() > 0;
}

bool WifiCredentialStore::save(const char* ssid, const char* password) {
    if (!ssid || ssid[0] == '\0') {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) {
        ESP_LOGE(TAG, "Cannot open NVS namespace '%s'", NAMESPACE);
        return false;
    }
    bool ok = prefs.putString(KEY_SSID, ssid) > 0;
    // putString() returns the bytes written, which is 0 for an empty (open
    // network) password, so confirm that one by checking the key exists.
    prefs.putString(KEY_PASSWORD, password ? password : "");
    ok = ok && prefs.isKey(KEY_PASSWORD);
    prefs.end();
    return ok;
}
