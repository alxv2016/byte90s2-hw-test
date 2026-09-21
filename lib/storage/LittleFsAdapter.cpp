/**
 * LittleFsAdapter.cpp
 *
 * Implementation for LittleFsAdapter.
 */

#include "LittleFsAdapter.h"

#include <FS.h>
#include <LittleFS.h>
#include <esp_log.h>

static const char* TAG = "LittleFsAdapter";

LittleFsAdapter::LittleFsAdapter()
    : _mounted(false)
{
}

LittleFsAdapter::~LittleFsAdapter() {
    end();
}

FSStatus LittleFsAdapter::begin() {
    if (_mounted) {
        ESP_LOGD(TAG, "LittleFS already mounted");
        return FSStatus::FS_SUCCESS;
    }

    ESP_LOGI(TAG, "Mounting LittleFS...");

    // Mount LittleFS from "assets" partition at /assets
    if (!LittleFS.begin(false, "/assets", 10, "assets")) {
        ESP_LOGE(TAG, "❌ Failed to mount LittleFS from 'assets' partition");
        return FSStatus::FS_MOUNT_FAILED;
    }

    _mounted = true;

    ESP_LOGI(TAG, "✅ LittleFS mounted successfully");
    ESP_LOGD(TAG, "  Total: %.2f MB", LittleFS.totalBytes() / (1024.0 * 1024.0));
    ESP_LOGD(TAG, "  Used: %.2f MB", LittleFS.usedBytes() / (1024.0 * 1024.0));
    ESP_LOGD(TAG, "  Free: %.2f MB", (LittleFS.totalBytes() - LittleFS.usedBytes()) / (1024.0 * 1024.0));

    return FSStatus::FS_SUCCESS;
}

void LittleFsAdapter::end() {
    if (_mounted) {
        LittleFS.end();
        _mounted = false;
        ESP_LOGI(TAG, "LittleFS unmounted");
    }
}

bool LittleFsAdapter::exists(const char* path) {
    if (!_mounted) {
        ESP_LOGW(TAG, "Cannot check existence: filesystem not mounted");
        return false;
    }
    return LittleFS.exists(path);
}

fs::File LittleFsAdapter::open(const char* path, const char* mode) {
    if (!_mounted) {
        ESP_LOGW(TAG, "Cannot open file: filesystem not mounted");
        return fs::File();
    }
    return LittleFS.open(path, mode);
}
