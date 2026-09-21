/**
 * RuntimeTaskRegistry.h
 *
 * Centralized FreeRTOS task creation and lifecycle management
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <map>
#include <vector>

/**
 * Cleanup pattern for task lifecycle
 */
enum class CleanupPattern {
    GRACEFUL,             // Graceful shutdown with polling (AudioService pattern)
    GRACEFUL_THEN_FORCE,  // Poll with timeout, then force delete
    FORCE_DELETE,         // vTaskDelete() immediately
    SELF_DELETING         // Task calls vTaskDelete(nullptr) on itself (one-shot task pattern)
};

/**
 * Task metadata tracked by RuntimeTaskRegistry
 */
struct TaskMetadata {
    const char* name;                 // Task name (e.g., "audio_capture")
    const char* component;            // Owning component (e.g., "AudioService")
    TaskHandle_t handle;              // FreeRTOS task handle (owned by RuntimeTaskRegistry)
    UBaseType_t priority;             // FreeRTOS priority (0-24)
    BaseType_t core;                  // Core affinity (0, 1, or tskNO_AFFINITY)
    uint32_t stack_size;              // Stack size in bytes
    CleanupPattern cleanup_pattern;   // How task is cleaned up
    const char* description;          // Optional description

    // Runtime monitoring data
    uint32_t created_at_ms;           // When task was created
    UBaseType_t high_water_mark;      // Lowest free stack (in words)
    bool is_active;                   // Currently active
    uint32_t cleanup_timeout_ms;      // Optional cleanup timeout override
};

/**
 * RuntimeTaskRegistry - Centralized task creation and monitoring
 *
 * Provides:
 * - Centralized task creation via createTask()
 * - FreeRTOS lifecycle management (stopTask handles cleanup patterns)
 * - Health monitoring (stack usage)
 * - Centralized visibility and debugging
 * - Minimal overhead (<1KB RAM, <0.01% CPU)
 *
 * Usage:
 *   // Component creates task via RuntimeTaskRegistry
 *   bool created = RuntimeTaskRegistry::instance().createTask(
 *       "audio_capture",
 *       "AudioService",
 *       captureTask,
 *       this,
 *       5,
 *       0,
 *       24576,
 *       CleanupPattern::GRACEFUL
 *   );
 *   if (!created) {
 *       // Handle task creation failure
 *   }
 *
 *   // Component stops task (preps state, then calls stopTask)
 *   _capture_active = false;
 *   RuntimeTaskRegistry::instance().stopTask("audio_capture");
 */
class RuntimeTaskRegistry {
public:
    /**
     * Get singleton instance
     */
    static RuntimeTaskRegistry& instance();

    /**
     * Initialize RuntimeTaskRegistry (call once during setup)
     */
    bool begin();

    /**
     * Create a new FreeRTOS task
     *
     * Components call this instead of xTaskCreatePinnedToCore() directly.
     * RuntimeTaskRegistry owns the task handle and manages lifecycle.
     *
     * @param name Task name (must be unique, used for FreeRTOS and lookups)
     * @param component Component name (e.g., "AudioService")
     * @param task_function Static task function from component
     * @param parameter Parameter passed to task (usually 'this' pointer)
     * @param priority FreeRTOS priority (0-24, higher = higher priority)
     * @param core Core affinity (0, 1, or tskNO_AFFINITY)
     * @param stack_size Stack size in bytes
     * @param cleanup_pattern How to cleanup when stopTask() is called
     * @param description Optional description for debugging
     * @param cleanup_timeout_ms Optional override for cleanup wait timeout
     * @return true if task was created successfully
     */
    bool createTask(
        const char* name,
        const char* component,
        TaskFunction_t task_function,
        void* parameter,
        UBaseType_t priority,
        BaseType_t core,
        uint32_t stack_size,
        CleanupPattern cleanup_pattern,
        const char* description = nullptr,
        uint32_t cleanup_timeout_ms = 0
    );

    /**
     * Stop and cleanup a task
     *
     * Components should:
     * 1. Set their internal flags (e.g., _capture_active = false)
     * 2. Disable hardware (e.g., _codec->enableInput(false))
     * 3. Drain queues to unblock task
     * 4. Call stopTask() - RuntimeTaskRegistry handles FreeRTOS cleanup
     *
     * RuntimeTaskRegistry executes the registered cleanup pattern:
     * - GRACEFUL: Polls with timeout, logs warnings if stuck
     * - FORCE_DELETE: Immediately calls vTaskDelete()
     * - SELF_DELETING: Waits for task to self-exit
     *
     * @param name Task name
     */
    void stopTask(const char* name);

    /**
     * Mark a self-deleting task as stopped
     *
     * Call this from the task entrypoint before vTaskDelete(nullptr)
     * when the task completes on its own (no external stopTask call).
     *
     * @param name Task name
     */
    void markTaskStopped(const char* name);

    /**
     * Check if a task exists and its FreeRTOS handle is live.
     *
     * @param name Task name
     * @return true if task exists and handle is valid
     */
    bool isTaskActive(const char* name) const;

    /**
     * Get metadata for a specific task
     *
     * @param name Task name
     * @return Pointer to metadata, or nullptr if not found
     */
    const TaskMetadata* getTaskInfo(const char* name) const;

    /**
     * Get all tasks owned by a component
     *
     * @param component Component name (e.g., "AudioService")
     * @return Vector of task names
     */
    std::vector<const char*> getTasksByComponent(const char* component) const;

    /**
     * Get count of registered tasks
     */
    size_t getTaskCount() const;

    /**
     * Get count of live tasks
     */
    size_t getActiveTaskCount() const;

    /**
     * Get total stack memory allocated across all tasks
     */
    uint32_t getTotalStackMemory() const;

    /**
     * Check stack health for all tasks
     *
     * @param threshold_bytes Warn if free stack below this
     * @return Count of tasks below threshold
     */
    int checkStackHealth(uint32_t threshold_bytes = 2048);

    /**
     * Print comprehensive health report to ESP_LOG
     */
    void printHealthReport(bool include_inactive = false);

    /**
     * List all registered tasks in table format
     */
    void listTasks();

private:
    RuntimeTaskRegistry();
    ~RuntimeTaskRegistry() = default;

    // Prevent copying (singleton)
    RuntimeTaskRegistry(const RuntimeTaskRegistry&) = delete;
    RuntimeTaskRegistry& operator=(const RuntimeTaskRegistry&) = delete;

    void updateTaskMetricsLocked(TaskMetadata& task);
    static uint32_t wordsToBytes(UBaseType_t words);
    TaskMetadata* findTask(const char* name);
    const TaskMetadata* findTask(const char* name) const;

    std::map<String, TaskMetadata> _tasks;
    SemaphoreHandle_t _mutex;
    bool _initialized;
};
