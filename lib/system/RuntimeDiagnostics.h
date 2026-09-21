/**
 * RuntimeDiagnostics.h
 *
 * Centralized runtime diagnostics for memory, tasks, and reset reporting.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class RuntimeDiagnostics {
public:
    static RuntimeDiagnostics& instance();

    void begin();
    void logSnapshot(const char* reason,
                     bool include_task_report = true,
                     bool include_inactive_tasks = false);
    void logTaskCreateFailure(const char* task_name,
                              const char* component,
                              uint32_t requested_stack_bytes);
    void reportStackOverflow(const char* task_name);
    static const char* resetReasonToString(uint32_t reason);

private:
    RuntimeDiagnostics() = default;
    ~RuntimeDiagnostics() = default;

    RuntimeDiagnostics(const RuntimeDiagnostics&) = delete;
    RuntimeDiagnostics& operator=(const RuntimeDiagnostics&) = delete;

    void logMemorySnapshotLocked(const char* reason) const;

    bool _initialized = false;
};
