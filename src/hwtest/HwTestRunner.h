/**
 * HwTestRunner.h
 *
 * Owns the hardware tests and steps through them. Test 0 (startup GIF) runs
 * once at power-on; tests 1..N then cycle on button clicks.
 *
 * Button while a test is finished (the footer scrolls this hint):
 *   click         -> next test (wraps from the last test back to test 1)
 *   double-click  -> rerun the current test
 *   hold          -> nothing; a 1 s hold is only an input inside the button
 *                    test, which buzzes when one registers
 *
 * Holding the key for 6 s at any time opens the PowerMenu overlay (Cancel /
 * Restart / Shutdown). A test running at that moment is abandoned; Cancel
 * restarts it, or restores the result page if the test had finished.
 *
 * A single click is only reported once the double-click window has passed,
 * so "next" fires about half a second after the click.
 *
 * Serial (115200) mirrors the button and adds WiFi provisioning:
 *   n                     next test (skips a running test)
 *   r                     rerun current test
 *   s                     print result summary
 *   wifi <ssid> <pass>    save WiFi credentials to NVS
 */

#pragma once

#include "hwtest/HardwareTest.h"
#include "hwtest/HwTestContext.h"
#include "hwtest/PowerMenu.h"
#include "hwtest/TestScreen.h"

#include <Arduino.h>

class HwTestRunner {
public:
    HwTestRunner(HwTestContext& context, TestScreen& screen);
    ~HwTestRunner();

    /**
     * @brief Register button callbacks and start the startup GIF test
     */
    void begin();
    void loop();

private:
    enum class State : uint8_t {
        RUNNING,
        WAITING,
    };

    // Result slot for a test that has not run since boot.
    enum class Outcome : uint8_t {
        NOT_RUN,
        PASSED,
        FAILED,
        SKIPPED,
    };

    // Button input after single/double-click classification.
    enum class ButtonEvent : uint8_t {
        NONE,
        CLICK,
        DOUBLE_CLICK,
        LONG_PRESS,
    };

    static constexpr uint8_t TEST_COUNT = 11;  // startup GIF + 10 cycled tests
    static constexpr uint8_t FIRST_CYCLED_TEST = 1;

    ButtonEvent classifyButton(bool click, bool long_press);
    bool holdReachedPowerMenu(bool long_press);
    void openPowerMenu();
    void handlePowerMenu(ButtonEvent event);
    void startTest(uint8_t index);
    void finishTest(Outcome outcome);
    void advance();
    void pollSerial();
    void handleSerialCommand(String line);
    void printSummary() const;
    static const char* outcomeName(Outcome outcome);
    static const char* buttonEventName(ButtonEvent event);

    HwTestContext& _context;
    TestScreen& _screen;
    HardwareTest* _tests[TEST_COUNT];
    Outcome _outcomes[TEST_COUNT];
    uint8_t _current;
    State _state;
    uint32_t _test_started_ms;
    bool _click_pending;
    bool _long_press_pending;
    bool _single_click_pending;
    uint32_t _first_click_ms;
    PowerMenu _power_menu;
    bool _hold_active;
    uint32_t _hold_started_ms;
    bool _menu_interrupted_test;
    String _serial_line;
};
