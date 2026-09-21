# Coding Style Guide

**Scope:** naming, file organization, and formatting rules for BYTE-90 firmware.

**Source of truth:** this document. All code contributions must follow it.

**Read this when:** writing or reviewing any C++ in `src/` or `lib/`.

---

## Rules At A Glance

Everything below this table is worked examples of these rules. Scan here first;
read the section only when you need the pattern.

### Naming

| Thing | Convention | Example |
| --- | --- | --- |
| Files | `PascalCase.cpp` / `.h` | `AnimationController.h` |
| Classes | `PascalCase` | `LocalizationCatalog` |
| Methods | `camelCase()` | `setLanguage()` |
| Local variables | `snake_case` | `lang_code` |
| Member variables | `_snake_case` | `_current_language` |
| Constants and macros | `SCREAMING_SNAKE_CASE` | `DEFAULT_LANGUAGE` |
| Enum values | `SCREAMING_SNAKE_CASE` | `LANG_SUCCESS` |

### Formatting

| Rule | Value |
| --- | --- |
| Indentation | 4 spaces, no tabs |
| Braces | K&R — opening brace on the same line |
| Line length | 80–100 chars, hard max 120 |
| Header guard | `#pragma once` |

### C++ Patterns

| Rule | Detail |
| --- | --- |
| Initialization | Always use constructor initialization lists |
| `const` | Use liberally for correctness |
| Getters | `is*()` for bool, `get*()` for values |
| State | Validate before use; guard on a readiness flag |

### ESP32 / Arduino

| Rule | Detail |
| --- | --- |
| FreeRTOS tasks | Static entry point + instance implementation |
| Queues | Typed structs; **always check return values** |
| Logging | `ESP_LOG*` with a file-local `TAG` |
| Pins | `#define` with descriptive names |

**Consistency beats any individual rule.** When in doubt, follow the patterns in
surrounding code.

---

## Sections

1. [Naming Conventions](#1-naming-conventions)
2. [File Organization](#2-file-organization)
3. [Code Formatting](#3-code-formatting)
4. [C++ Patterns](#4-c-patterns)
5. [ESP32/Arduino Specific](#5-esp32arduino-specific)
6. [Documentation](#6-documentation)

---

## 1. Naming Conventions

### 1.1 File Names

**Use PascalCase for all source files:**

```
✓ MyClass.h
✓ MyClass.cpp
✓ NetworkClient.h
✓ StateManager.cpp
✓ UserInterface.h

✗ my_class.h
✗ network-client.cpp
✗ state_manager.h
✗ userInterface.cpp
```

**Rationale:**
- Matches class names (PascalCase) for easy identification
- Clear correspondence between file name and primary class
- Consistent with many C++ projects and frameworks
- Makes it obvious which files contain which classes

### 1.2 Class Names

**Use PascalCase:**

```cpp
✓ class MyClass { };
✓ class NetworkClient { };
✓ class StateManager { };

✗ class my_class { };
✗ class Network_Client { };
```

### 1.3 Function and Method Names

**Use camelCase:**

```cpp
✓ bool begin();
✓ void startCapture();
✓ int getSampleRate() const;
✓ bool isProcessingActive() const;

✗ bool Begin();
✗ void start_capture();
✗ int get_sample_rate() const;
```

### 1.4 Variable Names

**Local variables: snake_case**

```cpp
✓ int sample_rate = 16000;
✓ bool is_initialized = false;
✓ unsigned long last_tick = 0;

✗ int SampleRate;
✗ bool isInitialized;
```

**Member variables: snake_case with leading underscore**

```cpp
class AudioEngine {
private:
    ✓ int _sample_rate;
    ✓ bool _initialized;
    ✓ TaskHandle_t _task_handle;

    ✗ int sampleRate;
    ✗ bool m_initialized;
    ✗ TaskHandle_t taskHandle_;
};
```

**Global/static variables: snake_case**

```cpp
✓ static const char* TAG = "MyClass";
✓ NetworkClient* client = nullptr;
✓ String session_id = "";

✗ static const char* Tag;
✗ NetworkClient* NetworkClient;
```

### 1.5 Constants and Macros

**Use SCREAMING_SNAKE_CASE:**

```cpp
✓ #define SAMPLE_RATE 16000
✓ #define SPI_SCK_PIN 7
✓ #define INPUT_PIN 8
✓ const int MAX_BUFFER_SIZE = 1024;

✗ #define sampleRate 16000
✗ #define SpiSckPin 7
```

### 1.6 Enum Values

**Always use SCREAMING_SNAKE_CASE:**

```cpp
✓ enum class InputEvent {
    NONE,
    PRESS,
    DOUBLE_PRESS,
    LONG_PRESS
};

✓ enum class State {
    UNKNOWN = 0,
    INITIALIZING = 1,
    READY = 2,
    ACTIVE = 3,
    IDLE = 4
};

✓ typedef enum {
    STATUS_UNKNOWN = 0,
    STATUS_NOT_READY,
    STATUS_READY,
    STATUS_COMPLETE
} status_t;

✗ enum State {
    StateUnknown = 0,    // Don't use PascalCase
    StateInitializing = 1
};
```

### 1.7 Type Definitions

**Use snake_case with _t suffix:**

```cpp
✓ typedef void (*DataCallback)(const uint8_t* data, int len, void* user_data);
✓ typedef enum { ... } status_t;
✓ typedef struct { ... } info_t;

✗ typedef void (*dataCallback)(...);
✗ typedef enum { ... } Status;
```

---

## 2. File Organization

### 2.1 Header Guards

**Always use `#pragma once` (preferred):**

```cpp
✓ #pragma once

#include <Arduino.h>

class MyClass {
    // ...
};
```

**Only use traditional guards for compatibility with older tools:**

```cpp
// Only if required for tool compatibility
#ifndef MY_CLASS_H
#define MY_CLASS_H

// ...

#endif // MY_CLASS_H
```

### 2.2 Include Order

**Follow this order:**

1. Own header (for .cpp files)
2. Project headers (alphabetically)
3. Arduino/ESP32 libraries
4. Standard C/C++ libraries

```cpp
// MyClass.cpp
#include "MyClass.h"                // 1. Own header

#include "NetworkClient.h"          // 2. Project headers
#include "DataProcessor.h"

#include <Arduino.h>                // 3. Arduino/ESP32
#include <esp_log.h>
#include <FreeRTOS.h>

#include <string.h>                 // 4. Standard library
```

**Project headers in main.cpp:**

```cpp
// Group related headers together
#include "Display.h"
#include "UIDriver.h"
#include "StateManager.h"
#include "NetworkClient.h"

#include "DataProcessor.h"
#include "DataService.h"

#include "Input.h"
#include "Hardware.h"

#include <Arduino.h>
#include <ArduinoJson.h>
```

### 2.3 Header File Structure

```cpp
#pragma once

// 1. System includes
#include <Arduino.h>
#include <FreeRTOS.h>

// 2. Project includes
#include "OtherClass.h"

// 3. Constants and macros
#define SAMPLE_RATE 16000
#define MAX_BUFFER_SIZE 1024

// 4. Forward declarations (if needed)
class DataProcessor;

// 5. Type definitions
typedef void (*DataCallback)(const uint8_t* data, int len);

enum class State {
    IDLE,
    ACTIVE
};

// 6. Class declaration
class MyClass {
public:
    // Public interface

private:
    // Private implementation
};

// 7. Inline functions (if any)
inline bool isValid(int value) {
    return value >= 0 && value <= 100;
}
```

### 2.4 Implementation File Structure

```cpp
// 1. Own header
#include "MyClass.h"

// 2. Other includes
#include <esp_log.h>
#include <string.h>

// 3. Static TAG for logging
static const char* TAG = "MyClass";

// 4. Static/anonymous namespace variables
static int global_counter = 0;

// 5. Constructor
MyClass::MyClass(int sample_rate)
    : _sample_rate(sample_rate),
      _initialized(false) {
}

// 6. Destructor
MyClass::~MyClass() {
    cleanup();
}

// 7. Public methods (in header declaration order)
bool MyClass::initialize() {
    // Implementation
}

void MyClass::cleanup() {
    // Implementation
}

// 8. Private methods
void MyClass::processingTaskImpl() {
    // Implementation
}

// 9. Static methods
void MyClass::processingTask(void* parameter) {
    // Implementation
}
```

### 2.5 Class Member Ordering

**Order: Public → Private, grouped by functionality**

```cpp
class MyClass {
public:
    // 1. Constructor/Destructor
    MyClass(int sample_rate);
    ~MyClass();

    // 2. Lifecycle methods
    bool initialize();
    void cleanup();
    bool start();
    void stop();

    // 3. Configuration methods (grouped by feature)
    void enableFeatureA(bool enable);
    void enableFeatureB(bool enable);
    void enableFeatureC(bool enable);

    // 4. Core functionality
    bool processData(const int16_t* data, int samples);
    bool getData(int16_t* data, int max_samples);

    // 5. Getters (inline if simple)
    bool isActive() const { return _active; }
    int getSampleRate() const { return _sample_rate; }

private:
    // 1. Static functions (FreeRTOS tasks)
    static void processingTask(void* parameter);

    // 2. Private helper methods
    void processingTaskImpl();

    // 3. Member variables (grouped logically)
    // Configuration
    int _sample_rate;
    int _frame_size;

    // State flags
    bool _initialized;
    bool _active;

    // FreeRTOS primitives
    TaskHandle_t _task_handle;
    QueueHandle_t _queue;

    // Buffers
    int16_t* _buffer;
};
```

---

## 3. Code Formatting

### 3.1 Indentation

**Use 4 spaces (NO tabs):**

```cpp
✓ void MyClass::initialize() {
    if (!_initialized) {
        ESP_LOGI(TAG, "Initializing...");
        _initialized = true;
    }
}

✗ void MyClass::initialize() {
→   if (!_initialized) {
→   →   ESP_LOGI(TAG, "Initializing...");
→   →   _initialized = true;
→   }
}
```

### 3.2 Brace Style

**Function definitions: Opening brace on same line**

```cpp
✓ void MyClass::processingTaskImpl() {
    ESP_LOGI(TAG, "Task started");
    // Implementation
}

✗ void MyClass::processingTaskImpl()
{
    ESP_LOGI(TAG, "Task started");
}
```

**Control structures: Opening brace on same line**

```cpp
✓ if (condition) {
    // Code
}

✓ while (active) {
    // Code
}

✓ for (int i = 0; i < count; i++) {
    // Code
}

✓ switch (value) {
    case 1:
        break;
    default:
        break;
}
```

**Class/struct definitions: Opening brace on same line**

```cpp
✓ class MyClass {
public:
    // Members
};

✓ struct DataFrame {
    int16_t data[160];
    int samples;
};
```

**Short inline functions: Same line acceptable**

```cpp
✓ bool isActive() const { return _active; }
✓ int getSampleRate() const { return _sample_rate; }
```

### 3.3 Line Length

**Target: 80-100 characters, maximum 120 characters**

**Break long lines appropriately:**

```cpp
// Long parameter lists: Indent to align or one per line
processor = new DataProcessor(
    INPUT_SAMPLE_RATE,   // Input sample rate
    OUTPUT_SAMPLE_RATE,  // Output sample rate
    PIN_BCLK,
    PIN_LRC,
    PIN_DOUT
);

// Long conditions
if (!_input_buffer || !_output_buffer || !_temp_buffer) {
    ESP_LOGE(TAG, "Failed to allocate processing buffers");
    return false;
}

// Long string concatenation
String message = "This is a very long message that needs to be "
                 "split across multiple lines for better "
                 "readability";
```

### 3.4 Spacing

**Around operators:**

```cpp
✓ int result = a + b * c;
✓ bool flag = (value > 10) && (value < 20);
✓ if (x == 5) { }

✗ int result=a+b*c;
✗ if(x==5){ }
```

**After commas:**

```cpp
✓ void function(int a, int b, int c);
✓ int array[] = {1, 2, 3, 4};

✗ void function(int a,int b,int c);
```

**Inside parentheses (no extra spaces):**

```cpp
✓ if (condition) { }
✓ for (int i = 0; i < 10; i++) { }

✗ if ( condition ) { }
✗ for ( int i = 0; i < 10; i++ ) { }
```

### 3.5 Comments

**File/class documentation: Block comments**

```cpp
/**
 * DataProcessor - Real-time data processing
 *
 * Features:
 * - Feature A (Description)
 * - Feature B (Description)
 * - Feature C (Description)
 * - Feature D (Description)
 *
 * Architecture:
 * - Runs in dedicated FreeRTOS task (Priority 6)
 * - Processes data in 10ms frames (160 samples at 16kHz)
 */
class DataProcessor {
    // ...
};
```

**Inline comments: Single line**

```cpp
// Initialize display
display->begin();

// Calculate frame size (10ms at 16kHz = 160 samples)
int frame_size = sample_rate * 10 / 1000;

isListening = false;  // Track listening state
```

**Section separators:**

```cpp
// ========================================================================
// AUDIO TOOLS
// ========================================================================

// ========================================================================
// STATUS TOOLS
// ========================================================================
```

**Complex logic explanation:**

```cpp
// Disable auto-reconnect to prevent race condition during cleanup
// The cleanup happens asynchronously, and calling loop() during
// cleanup can cause heap corruption when checking connection status
if (client) {
    client->setReconnectInterval(0);  // Disable auto-reconnect
}
```

---

## 4. C++ Patterns

### 4.1 Constructor Initialization Lists

**Always use initialization lists, multi-line with alignment:**

```cpp
✓ MyClass::MyClass(int sample_rate, int channels)
    : _sample_rate(sample_rate),
      _channels(channels),
      _frame_size(160),
      _initialized(false),
      _active(false),
      _task_handle(nullptr),
      _queue(nullptr),
      _buffer(nullptr) {
}

✗ MyClass::MyClass(int sample_rate, int channels) {
    _sample_rate = sample_rate;
    _channels = channels;
    _frame_size = 160;
}
```

### 4.2 Const Correctness

**Use const for:**
- Methods that don't modify state
- Parameters that won't be modified
- Return values that shouldn't be modified

```cpp
✓ bool isActive() const { return _active; }
✓ int getSampleRate() const { return _sample_rate; }
✓ void processData(const int16_t* data, int samples);
✓ const char* getTag() const { return TAG; }
```

### 4.3 Getter/Setter Patterns

**Getters: Inline const methods (simple) or implemented in .cpp (complex)**

```cpp
// Simple getters: inline in header
bool isActive() const { return _active; }
int getSampleRate() const { return _sample_rate; }

// Complex getters: in .cpp
int getQueuedFrames() const;  // Needs mutex, queue access

// Boolean state: is* prefix
bool isReady() const;
bool isProcessing() const;
bool isVoiceDetected() const;

// Value access: get* prefix
int getVolume() const;
float getGain() const;
```

**Setters: Non-const methods**

```cpp
void setVolume(int volume);
void setGain(float gain);
void setBitrate(int bitrate);

// Boolean setters: enable* prefix
void enableInput(bool enable);
void enableOutput(bool enable);
void enableProcessing(bool enable);
```

### 4.4 Initialization Pattern

**Check initialization state and handle partial initialization failures:**

```cpp
bool MyClass::initialize() {
    if (_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    // Create resources
    _queue = xQueueCreate(10, sizeof(DataFrame));
    if (!_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        return false;
    }

    _buffer = (int16_t*)malloc(BUFFER_SIZE * sizeof(int16_t));
    if (!_buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        cleanup();  // Cleanup partial initialization
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "✅ Initialization complete");
    return true;
}
```

### 4.5 State Validation

**Validate state before performing operations:**

```cpp
bool MyClass::startProcessing() {
    if (!_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    if (_processing_active) {
        ESP_LOGW(TAG, "Already processing");
        return true;
    }

    // Start processing
    _processing_active = true;
    return true;
}
```

---

## 5. ESP32/Arduino Specific

### 5.1 Setup/Loop Pattern

**Standard Arduino structure:**

```cpp
void setup() {
    ESP_LOGI(TAG, "::: System Starting :::");

    // 1. Hardware initialization
    ESP_LOGI(TAG, "Display...");
    display = new Display(...);
    display->begin();
    ESP_LOGI(TAG, "✅ Display ready");

    // 2. Subsystem initialization
    ESP_LOGI(TAG, "Processor...");
    processor = new DataProcessor(...);
    if (processor->initialize()) {
        ESP_LOGI(TAG, "✅ Processor ready");
    } else {
        ESP_LOGE(TAG, "❌ Processor failed");
    }

    // 3. Service initialization
    // ...

    ESP_LOGI(TAG, "::: System Ready :::");
}

void loop() {
    // Update timing
    unsigned long now = millis();
    lv_tick_inc(now - last_tick);
    last_tick = now;

    // Update UI framework
    lv_timer_handler();

    // Update input state
    if (input) {
        input->update();
    }

    // Process services
    service1->loop();
    service2->loop();

    // Periodic tasks
    static unsigned long last_update = 0;
    if (now - last_update > 2000) {
        last_update = now;
        updateStatus();
    }

    delay(5);  // Yield so the top-level loop can keep making progress
}
```

**Watchdog rule:** shared/library code should not reset the caller's ESP task
watchdog. For `loopTask`, TWDT feeding belongs in the top-level application loop
at clear phase boundaries. Inner loops should use bounded work, timeouts, and
`yield()` instead of hidden watchdog resets.

### 5.2 FreeRTOS Task Pattern

**Use static entry point + instance implementation:**

```cpp
class MyClass {
private:
    static void processingTask(void* parameter);
    void processingTaskImpl();
    TaskHandle_t _task_handle;
};

// Static entry point
void MyClass::processingTask(void* parameter) {
    MyClass* instance = static_cast<MyClass*>(parameter);
    instance->processingTaskImpl();
}

// Instance implementation
void MyClass::processingTaskImpl() {
    ESP_LOGI(TAG, "Processing task started");

    while (_processing_active) {
        // Task work
    }

    ESP_LOGI(TAG, "Processing task ended");
    vTaskDelete(nullptr);  // Self-delete
}

// Task creation
bool MyClass::startProcessing() {
    xTaskCreate(
        processingTask,       // Function
        "data_process",       // Name (lowercase_snake_case)
        8192,                 // Stack size
        this,                 // Parameter
        6,                    // Priority
        &_task_handle         // Handle
    );
}
```

**Task priorities (established hierarchy):**
- Priority 8: Data capture (time-critical I/O)
- Priority 6: Data processing
- Priority 4: Data output (time-critical I/O)
- Priority 2: Heavy operations (CPU-intensive)

**Task naming:**
- Static function: `*Task`
- Implementation: `*TaskImpl`
- Task name string: `lowercase_snake_case`

### 5.3 FreeRTOS Queue Pattern

```cpp
// Queue handle
QueueHandle_t _queue;

// Queue item structure
struct DataFrame {
    int16_t data[160];
    int samples;
};

// Creation
_queue = xQueueCreate(10, sizeof(DataFrame));
if (!_queue) {
    ESP_LOGE(TAG, "Failed to create queue");
    return false;
}

// Send
DataFrame frame;
memcpy(frame.data, data, samples * sizeof(int16_t));
frame.samples = samples;
if (xQueueSend(_queue, &frame, 0) != pdTRUE) {
    return false;  // Queue full
}

// Receive with timeout
DataFrame frame;
if (xQueueReceive(_queue, &frame, pdMS_TO_TICKS(100)) == pdTRUE) {
    // Process frame
}

// Cleanup
if (_queue) {
    xQueueReset(_queue);   // Clear first
    vQueueDelete(_queue);
    _queue = nullptr;
}
```

### 5.4 Logging Pattern

**Use ESP-IDF logging macros:**

```cpp
#include <esp_log.h>

static const char* TAG = "MyClass";

// Log levels
ESP_LOGI(TAG, "Initializing: %d Hz, %d ch", sample_rate, channels);
ESP_LOGW(TAG, "Buffer overflow: %d frames dropped", dropped);
ESP_LOGE(TAG, "Failed to allocate memory: %d bytes", size);
ESP_LOGD(TAG, "Processing frame %d", frame_count);

// Success/failure indicators
ESP_LOGI(TAG, "✅ Initialization complete");
ESP_LOGE(TAG, "❌ Initialization failed");
ESP_LOGW(TAG, "🟡  Running in degraded mode");

// Section separators - use simple format with colons
ESP_LOGI(TAG, ":::: System Starting ::::");
ESP_LOGI(TAG, ":::: Configuration Retrieved ::::");
ESP_LOGI(TAG, ":::: Network Connected ::::");
```

**Log Formatting Rules:**
- **Section headers:** Use `:::: Title ::::` format (simple, readable)
- **Avoid box-drawing characters:** Don't use `╔`, `║`, `╠`, `╚`, `═` characters
- **Keep it simple:** Terminal-friendly format that works across all platforms
- **Consistency:** Use the same format throughout the codebase

**Ownership Map (current codebase):**
- **Hardware/Driver**
  - `lib/i2c/SharedI2cBus.*` - Owns bus init and scan results.
  - `lib/power/Axp2101.*` - Owns PMIC init, button IRQs, and power telemetry.
  - `lib/audio/AudioCodec.*` - Owns I2S config, sample rates, DMA, channel enable/disable.
  - `lib/display/ArduinoSSD1351.*` - Owns display init details.
  - `lib/haptics/*` - Owns DRV2605 init/config.
- **Service/Subsystem**
  - `lib/storage/WifiCredentialStore.*` - Owns WiFi credential save/load in NVS.
  - `lib/storage/LittleFsAdapter.*` - Owns mount/unmount and storage stats.
  - `lib/gif_player/GifPlayer.*` - Owns GIF decode and frame-draw logs.
- **Application**
  - `src/hwtest/HwTestApp.*` - Owns bring-up; logs one ready/failed line per driver.
  - `src/hwtest/TestScreen.*` - Owns per-test start/Done/FAIL lines and result rows.
  - `src/hwtest/HwTestRunner.*` - Owns serial commands and the result summary.

**Log Level Rules:**
- **INFO:** One line per milestone; single owner.
- **WARN:** Recoverable issues (retrying), log once per failure path.
- **ERROR:** Unrecoverable or user-action needed.
- **DEBUG:** Noisy details (packet counts, buffer stats, frequent polling).

**TAG naming:**
- Matches class/module name
- Defined at file scope
- Used consistently throughout file

### 5.5 GPIO Pin Definitions

```cpp
// Hardware pins - Group related pins together
#define PERIPHERAL_A_SCK_PIN  7
#define PERIPHERAL_A_MOSI_PIN 9
#define PERIPHERAL_A_CS_PIN   44
#define PERIPHERAL_A_DC_PIN   43
#define PERIPHERAL_A_RST_PIN  1

// I2S pins
#define I2S_BCLK_PIN         4   // Bit Clock
#define I2S_LRC_PIN          2   // Left/Right Clock
#define I2S_DOUT_PIN         3   // Data Out
#define I2S_CLK_PIN          42  // PDM Clock
#define I2S_DATA_PIN         41  // PDM Data

// Button
#define BUTTON_PIN           8   // User button, active LOW
```

**Pin naming convention:**
- Format: `{PERIPHERAL}_{SIGNAL}_PIN` or `{PERIPHERAL}_{FUNCTION}`
- Include hardware-specific comments
- Group by peripheral/function
- Pass to constructors, don't hard-code

---

## 6. Documentation

### 6.1 File Headers

**Include brief description and key information:**

```cpp
/**
 * MyClass.h
 *
 * Real-time data processing component.
 * Provides filtering, transformation, and analysis.
 *
 * Author: Your Team
 * Board: ESP32-S3 Development Board
 */

#pragma once

#include <Arduino.h>
```

### 6.2 Class Documentation

```cpp
/**
 * DataProcessor - Real-time data processing pipeline
 *
 * Features:
 * - Feature A - Primary data transformation
 * - Feature B - Secondary filtering
 * - Feature C - Data validation
 * - Feature D - Output formatting
 *
 * Architecture:
 * - Runs in dedicated FreeRTOS task (Priority 6)
 * - Processes data in 10ms frames (160 samples @ 16kHz)
 * - Uses queues to avoid blocking capture task
 *
 * Usage:
 *   DataProcessor processor(16000, 1);
 *   processor.initialize();
 *   processor.enableFeatureA(true);
 *   processor.startProcessing();
 */
class DataProcessor {
    // ...
};
```

### 6.3 Function Documentation

**Always include @brief for function documentation:**

```cpp
/**
 * @brief Queue data frame for processing
 *
 * @param data Pointer to data samples (16-bit signed)
 * @param samples Number of samples in frame
 * @return true if queued successfully, false if queue full
 *
 * @note This is non-blocking. If queue is full, frame is dropped.
 */
bool queueDataFrame(const int16_t* data, int samples);

/**
 * @brief Initialize the data processor
 *
 * @return true if initialization successful, false otherwise
 */
bool initialize();

/**
 * @brief Enable or disable feature A
 *
 * @param enable true to enable Feature A, false to disable
 */
void enableFeatureA(bool enable);
```

**For simple getters/setters, @brief alone is sufficient:**

```cpp
/**
 * @brief Get the current sample rate
 */
int getSampleRate() const;

/**
 * @brief Check if processor is active
 */
bool isActive() const;

/**
 * @brief Set the configuration value
 *
 * @param value Configuration value (0-100)
 */
void setValue(int value);
```

### 6.4 Complex Logic Comments

```cpp
// Calculate optimal frame size for 10ms at given sample rate
// Example: 16kHz * 10ms = 160 samples
_frame_size = _sample_rate * 10 / 1000;

// Disable auto-reconnect to prevent race condition during cleanup
// The cleanup happens asynchronously, and calling update() during
// cleanup can cause issues when checking connection status
if (client) {
    client->setReconnectInterval(0);
}
```

---

---

The rules summary lives at the [top of this document](#rules-at-a-glance).
