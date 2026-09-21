/**
 * WifiCredentialStore.h
 *
 * WiFi SSID and password in NVS. Uses the same namespace and keys as the
 * product firmware's settings store ("wifi": "ssid", "password"), so
 * credentials saved by either firmware work in both.
 */

#pragma once

#include <Arduino.h>

class WifiCredentialStore {
public:
    /**
     * @brief Read the saved credentials
     * @return true if an SSID is saved; password may be empty (open network)
     */
    static bool load(String& ssid, String& password);

    /**
     * @brief Save credentials, replacing any saved ones
     * @param password May be empty for an open network
     */
    static bool save(const char* ssid, const char* password);
};
