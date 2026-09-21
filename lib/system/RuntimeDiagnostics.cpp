/**
 * RuntimeDiagnostics.cpp
 *
 * Centralized runtime diagnostics for memory, tasks, and reset reporting.
 */

#include "RuntimeDiagnostics.h"

#include "RuntimeTaskRegistry.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_system.h>

static const char* TAG = "RuntimeDiag";

RuntimeDiagnostics& RuntimeDiagnostics::instance() {
    static RuntimeDiagnostics instance;
    return instance;
}

void RuntimeDiagnostics::begin() {
    if (_initialized) {
        return;
    }

    _initialized = true;
    ESP_LOGI(TAG, "Runtime diagnostics initialized");
}

const char* RuntimeDiagnostics::resetReasonToString(uint32_t reason) {
    switch (static_cast<esp_reset_reason_t>(reason)) {
        case ESP_RST_UNKNOWN:
            return "unknown";
        case ESP_RST_POWERON:
            return "power_on";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt_watchdog";
        case ESP_RST_TASK_WDT:
            return "task_watchdog";
        case ESP_RST_WDT:
            return "watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deep_sleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        case ESP_RST_USB:
            return "usb";
        case ESP_RST_JTAG:
            return "jtag";
        case ESP_RST_EFUSE:
            return "efuse";
        default:
            return "other";
    }
}

void RuntimeDiagnostics::logMemorySnapshotLocked(const char* reason) const {
    const char* snapshot_reason = reason ? reason : "unknown";
    size_t free_heap = ESP.getFreeHeap();
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t largest_default = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    ESP_LOGI(TAG, ":::: Runtime Snapshot (%s) ::::", snapshot_reason);
    ESP_LOGI(TAG, "  Free heap:        %u bytes", static_cast<unsigned>(free_heap));
    ESP_LOGI(TAG, "  Free internal:    %u bytes", static_cast<unsigned>(free_internal));
    ESP_LOGI(TAG, "  Free PSRAM:       %u bytes", static_cast<unsigned>(free_psram));
    ESP_LOGI(TAG, "  Largest internal: %u bytes", static_cast<unsigned>(largest_internal));
    ESP_LOGI(TAG, "  Largest default:  %u bytes", static_cast<unsigned>(largest_default));
}

void RuntimeDiagnostics::logSnapshot(const char* reason,
                                     bool include_task_report,
                                     bool include_inactive_tasks) {
    if (!_initialized) {
        begin();
    }

    logMemorySnapshotLocked(reason);

    if (include_task_report) {
        RuntimeTaskRegistry::instance().printHealthReport(include_inactive_tasks);
    }

    ESP_LOGI(TAG, ":::: End Runtime Snapshot ::::");
}

void RuntimeDiagnostics::logTaskCreateFailure(const char* task_name,
                                              const char* component,
                                              uint32_t requested_stack_bytes) {
    ESP_LOGE(TAG,
             "Task create failure: name=%s component=%s requested_stack=%luB",
             task_name ? task_name : "<unknown>",
             component ? component : "<unknown>",
             static_cast<unsigned long>(requested_stack_bytes));
    logSnapshot("task_create_failed", true, true);
}

void RuntimeDiagnostics::reportStackOverflow(const char* task_name) {
    portDISABLE_INTERRUPTS();
    esp_rom_printf("STACK OVERFLOW: %s\n", task_name ? task_name : "<unknown>");
    while (true) {
    }
}
