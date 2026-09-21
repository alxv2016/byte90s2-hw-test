/**
 * RuntimeTaskRegistry.cpp
 *
 * Implementation of centralized FreeRTOS task management
 */

#include "RuntimeTaskRegistry.h"

#include "RuntimeDiagnostics.h"

#include <esp_log.h>

static const char* TAG = "RuntimeTaskRegistry";

uint32_t RuntimeTaskRegistry::wordsToBytes(UBaseType_t words) {
    return static_cast<uint32_t>(words) * sizeof(StackType_t);
}

// Singleton instance
RuntimeTaskRegistry& RuntimeTaskRegistry::instance() {
    static RuntimeTaskRegistry instance;
    return instance;
}

// Constructor
RuntimeTaskRegistry::RuntimeTaskRegistry()
    : _mutex(nullptr)
    , _initialized(false)
{
}

// Initialize
bool RuntimeTaskRegistry::begin() {
    if (_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "RuntimeTaskRegistry initialized");
    return true;
}

// Create task
bool RuntimeTaskRegistry::createTask(
    const char* name,
    const char* component,
    TaskFunction_t task_function,
    void* parameter,
    UBaseType_t priority,
    BaseType_t core,
    uint32_t stack_size,
    CleanupPattern cleanup_pattern,
    const char* description,
    uint32_t cleanup_timeout_ms
) {
    if (!_initialized) {
        ESP_LOGE(TAG, "RuntimeTaskRegistry not initialized");
        return false;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Check if task already exists
    auto existing = _tasks.find(name);
    if (existing != _tasks.end()) {
        if (!existing->second.is_active || existing->second.handle == nullptr) {
            // Allow recreation of inactive tasks (one-shot or completed)
            _tasks.erase(existing);
        } else {
            ESP_LOGE(TAG, "Task '%s' already exists", name);
            xSemaphoreGive(_mutex);
            return false;
        }
    }

    // Create the task
    TaskHandle_t handle = nullptr;
    BaseType_t result = xTaskCreatePinnedToCore(
        task_function,
        name,
        stack_size,
        parameter,
        priority,
        &handle,
        core
    );

    if (result != pdPASS || handle == nullptr) {
        ESP_LOGE(TAG, "Failed to create task '%s'", name);
        xSemaphoreGive(_mutex);
        RuntimeDiagnostics::instance().logTaskCreateFailure(
            name,
            component,
            stack_size
        );
        return false;
    }

    // Store metadata
    TaskMetadata metadata;
    metadata.name = name;
    metadata.component = component;
    metadata.handle = handle;
    metadata.priority = priority;
    metadata.core = core;
    metadata.stack_size = stack_size;
    metadata.cleanup_pattern = cleanup_pattern;
    metadata.description = description ? description : "";
    metadata.created_at_ms = millis();
    metadata.high_water_mark = stack_size / sizeof(StackType_t);
    metadata.is_active = true;
    metadata.cleanup_timeout_ms = cleanup_timeout_ms;

    _tasks[name] = metadata;

    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "Created task '%s' (%s) - priority=%d, core=%d, stack=%uKB",
             name, component, priority, core, stack_size / 1024);
    return true;
}

// Stop task
void RuntimeTaskRegistry::stopTask(const char* name) {
    if (!_initialized) {
        ESP_LOGE(TAG, "RuntimeTaskRegistry not initialized");
        return;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);

    TaskMetadata* task = findTask(name);
    if (!task) {
        ESP_LOGW(TAG, "Task '%s' not found", name);
        xSemaphoreGive(_mutex);
        return;
    }

    if (!task->handle) {
        ESP_LOGW(TAG, "Task '%s' already stopped", name);
        xSemaphoreGive(_mutex);
        return;
    }

    TaskHandle_t handle = task->handle;
    CleanupPattern pattern = task->cleanup_pattern;
    uint32_t timeout_ms = task->cleanup_timeout_ms;
    updateTaskMetricsLocked(*task);

    xSemaphoreGive(_mutex);

    auto wait_for_exit = [handle](uint32_t wait_ms) -> bool {
        if (!handle) {
            return true;
        }
        uint32_t ticks = pdMS_TO_TICKS(wait_ms);
        uint32_t start = xTaskGetTickCount();
        while ((xTaskGetTickCount() - start) < ticks) {
            eTaskState state = eTaskGetState(handle);
            if (state == eDeleted || state == eInvalid) {
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        return false;
    };

    auto resolve_timeout = [pattern, timeout_ms]() -> uint32_t {
        if (timeout_ms > 0) {
            return timeout_ms;
        }
        switch (pattern) {
            case CleanupPattern::GRACEFUL:
                return 2000;
            case CleanupPattern::GRACEFUL_THEN_FORCE:
                return 100;
            case CleanupPattern::SELF_DELETING:
                return 1000;
            case CleanupPattern::FORCE_DELETE:
            default:
                return 0;
        }
    };

    uint32_t resolved_timeout_ms = resolve_timeout();
    bool task_stopped = false;

    // Execute cleanup pattern
    switch (pattern) {
        case CleanupPattern::GRACEFUL: {
            // Poll with timeout (AudioService pattern)
            ESP_LOGI(TAG, "Stopping task '%s' gracefully...", name);
            bool exited = wait_for_exit(resolved_timeout_ms);

            if (!exited) {
                ESP_LOGW(TAG, "Task '%s' did not exit gracefully within %u ms timeout",
                         name, resolved_timeout_ms);
                // Note: We don't force delete - component may need more time
            } else {
                ESP_LOGI(TAG, "Task '%s' exited gracefully", name);
                task_stopped = true;
            }
            break;
        }

        case CleanupPattern::GRACEFUL_THEN_FORCE: {
            ESP_LOGI(TAG, "Stopping task '%s' gracefully (timeout=%u ms)...",
                     name, resolved_timeout_ms);
            bool exited = wait_for_exit(resolved_timeout_ms);
            if (!exited) {
                ESP_LOGW(TAG, "Task '%s' did not exit in %u ms - force deleting",
                         name, resolved_timeout_ms);
                vTaskDelete(handle);
                ESP_LOGI(TAG, "Task '%s' force deleted", name);
                task_stopped = true;
            } else {
                ESP_LOGI(TAG, "Task '%s' exited gracefully", name);
                task_stopped = true;
            }
            break;
        }

        case CleanupPattern::FORCE_DELETE: {
            if (handle) {
                eTaskState state = eTaskGetState(handle);
                if (state == eDeleted || state == eInvalid) {
                    ESP_LOGI(TAG, "Task '%s' already deleted", name);
                    task_stopped = true;
                    break;
                }
            }
            ESP_LOGI(TAG, "Force deleting task '%s'...", name);
            vTaskDelete(handle);
            ESP_LOGI(TAG, "Task '%s' deleted", name);
            task_stopped = true;
            break;
        }

        case CleanupPattern::SELF_DELETING: {
            // Just wait for task to self-exit
            ESP_LOGI(TAG, "Waiting for task '%s' to self-delete...", name);
            bool exited = wait_for_exit(resolved_timeout_ms);
            if (exited) {
                ESP_LOGI(TAG, "Task '%s' self-deleted", name);
                task_stopped = true;
            } else {
                ESP_LOGW(TAG, "Task '%s' did not self-delete within %u ms",
                         name, resolved_timeout_ms);
            }
            break;
        }
    }

    if (task_stopped) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        task = findTask(name);
        if (task) {
            task->handle = nullptr;
            task->is_active = false;
        }
        xSemaphoreGive(_mutex);
    }
}

void RuntimeTaskRegistry::markTaskStopped(const char* name) {
    if (!_initialized) {
        return;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    TaskMetadata* task = findTask(name);
    if (task) {
        updateTaskMetricsLocked(*task);
        task->handle = nullptr;
        task->is_active = false;
    }
    xSemaphoreGive(_mutex);
}

// Check if task is active
bool RuntimeTaskRegistry::isTaskActive(const char* name) const {
    if (!_initialized) return false;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    const TaskMetadata* task = findTask(name);
    bool active = task && task->handle != nullptr && task->is_active;
    xSemaphoreGive(_mutex);

    return active;
}

// Get task info
const TaskMetadata* RuntimeTaskRegistry::getTaskInfo(const char* name) const {
    if (!_initialized) return nullptr;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    const TaskMetadata* task = findTask(name);
    xSemaphoreGive(_mutex);

    return task;
}

// Get tasks by component
std::vector<const char*> RuntimeTaskRegistry::getTasksByComponent(const char* component) const {
    std::vector<const char*> result;

    if (!_initialized) return result;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    for (const auto& pair : _tasks) {
        if (strcmp(pair.second.component, component) == 0) {
            result.push_back(pair.second.name);
        }
    }

    xSemaphoreGive(_mutex);
    return result;
}

// Get task count
size_t RuntimeTaskRegistry::getTaskCount() const {
    if (!_initialized) return 0;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    size_t count = _tasks.size();
    xSemaphoreGive(_mutex);

    return count;
}

// Get live task count
size_t RuntimeTaskRegistry::getActiveTaskCount() const {
    if (!_initialized) return 0;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    size_t count = 0;
    for (const auto& pair : _tasks) {
        if (pair.second.handle != nullptr && pair.second.is_active) {
            count++;
        }
    }

    xSemaphoreGive(_mutex);
    return count;
}

// Get total stack memory
uint32_t RuntimeTaskRegistry::getTotalStackMemory() const {
    if (!_initialized) return 0;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    uint32_t total = 0;
    for (const auto& pair : _tasks) {
        total += pair.second.stack_size;
    }

    xSemaphoreGive(_mutex);
    return total;
}

// Check stack health
int RuntimeTaskRegistry::checkStackHealth(uint32_t threshold_bytes) {
    if (!_initialized) return 0;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    int warning_count = 0;
    for (auto& pair : _tasks) {
        TaskMetadata& task = pair.second;
        if (!task.handle || !task.is_active) continue;

        uint32_t free_bytes = wordsToBytes(task.high_water_mark);

        if (free_bytes < threshold_bytes) {
            ESP_LOGW(TAG, "Task '%s' low stack: %u bytes remaining (threshold=%u)",
                     task.name, free_bytes, threshold_bytes);
            warning_count++;
        }
    }

    xSemaphoreGive(_mutex);
    return warning_count;
}

// Print health report
void RuntimeTaskRegistry::printHealthReport(bool include_inactive) {
    if (!_initialized) {
        ESP_LOGW(TAG, "RuntimeTaskRegistry not initialized");
        return;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);

    constexpr uint32_t low_stack_threshold = 2048;
    size_t registered_tasks = _tasks.size();
    size_t live_tasks = 0;
    size_t inactive_tasks = 0;
    uint32_t registered_stack_bytes = 0;
    uint32_t live_stack_bytes = 0;
    int low_stack_tasks = 0;

    ESP_LOGI(TAG, ":::: Task Health Report ::::");
    for (auto& pair : _tasks) {
        TaskMetadata& task = pair.second;
        bool live_task = task.handle != nullptr && task.is_active;
        if (live_task) {
            updateTaskMetricsLocked(task);
            live_tasks++;
            live_stack_bytes += task.stack_size;
        } else {
            inactive_tasks++;
        }

        registered_stack_bytes += task.stack_size;

        uint32_t free_bytes = wordsToBytes(task.high_water_mark);
        uint32_t used_bytes = free_bytes < task.stack_size
                                  ? (task.stack_size - free_bytes)
                                  : 0;
        uint32_t free_pct = task.stack_size > 0
                                ? ((free_bytes * 100U) / task.stack_size)
                                : 0;
        const char* state_label = live_task ? "LIVE    " : "INACTIVE";

        if (!include_inactive && !live_task) {
            continue;
        }

        if (live_task && free_bytes < low_stack_threshold) {
            low_stack_tasks++;
        }

        ESP_LOGI(TAG,
                 "  %-14s %-12s %s p=%u c=%d stack=%luB used~%luB free=%luB (%lu%%)",
                 task.name,
                 task.component,
                 state_label,
                 static_cast<unsigned>(task.priority),
                 static_cast<int>(task.core),
                 static_cast<unsigned long>(task.stack_size),
                 static_cast<unsigned long>(used_bytes),
                 static_cast<unsigned long>(free_bytes),
                 static_cast<unsigned long>(free_pct));
    }
    ESP_LOGI(TAG,
             "Tasks: registered=%u live=%u low_stack=%d registered_stack=%luKB live_stack=%luKB",
             static_cast<unsigned>(registered_tasks),
             static_cast<unsigned>(live_tasks),
             low_stack_tasks,
             static_cast<unsigned long>(registered_stack_bytes / 1024),
             static_cast<unsigned long>(live_stack_bytes / 1024));
    if (!include_inactive && inactive_tasks > 0) {
        ESP_LOGI(TAG, "Inactive tasks omitted from detail: %u",
                 static_cast<unsigned>(inactive_tasks));
    }
    ESP_LOGI(TAG, ":::: End Task Report ::::");

    xSemaphoreGive(_mutex);
}

// List tasks
void RuntimeTaskRegistry::listTasks() {
    printHealthReport();
}

void RuntimeTaskRegistry::updateTaskMetricsLocked(TaskMetadata& task) {
    if (!task.handle || !task.is_active) {
        return;
    }

    task.high_water_mark = uxTaskGetStackHighWaterMark(task.handle);
}

// Find task by name (private, non-const)
TaskMetadata* RuntimeTaskRegistry::findTask(const char* name) {
    auto it = _tasks.find(name);
    if (it != _tasks.end()) {
        return &(it->second);
    }
    return nullptr;
}

// Find task by name (private, const)
const TaskMetadata* RuntimeTaskRegistry::findTask(const char* name) const {
    auto it = _tasks.find(name);
    if (it != _tasks.end()) {
        return &(it->second);
    }
    return nullptr;
}
