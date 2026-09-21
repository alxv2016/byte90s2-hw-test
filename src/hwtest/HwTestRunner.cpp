/**
 * HwTestRunner.cpp
 *
 * Implementation for HwTestRunner.
 */

#include "hwtest/HwTestRunner.h"

#include "Axp2101.h"
#include "DisplayColors.h"
#include "HapticsDriver.h"
#include "WifiCredentialStore.h"
#include "hwtest/tests/BatteryInfoTest.h"
#include "hwtest/tests/ButtonTest.h"
#include "hwtest/tests/DisplayTest.h"
#include "hwtest/tests/HapticsTest.h"
#include "hwtest/tests/ImuTest.h"
#include "hwtest/tests/MicTest.h"
#include "hwtest/tests/PowerTest.h"
#include "hwtest/tests/RtcTest.h"
#include "hwtest/tests/SoundTest.h"
#include "hwtest/tests/StartupGifTest.h"
#include "hwtest/tests/WifiTest.h"

#include <esp_log.h>

namespace {
static const char* TAG = "HwTestRunner";
constexpr size_t MAX_SERIAL_LINE = 160;
// Wider than the screen, so TestScreen scrolls it.
constexpr const char* FOOTER_HINT = "Click to proceed   |   Double click to restart";
// Second click within this window makes a double click. The AXP2101 reports
// clicks on release with 100 ms debounce, so this leaves room for a normal
// double tap without delaying single clicks much.
constexpr uint32_t DOUBLE_CLICK_WINDOW_MS = 450;
// The AXP2101 long-press IRQ fires this far into a hold (IRQ level set in
// AXP2101::begin()), so the hold started this long before the event.
constexpr uint32_t LONG_PRESS_IRQ_MS = 1000;
constexpr uint32_t POWER_MENU_HOLD_MS = 6000;
}  // namespace

HwTestRunner::HwTestRunner(HwTestContext& context, TestScreen& screen)
    : _context(context)
    , _screen(screen)
    , _tests{
          new StartupGifTest(context),
          new DisplayTest(context),
          new ButtonTest(context),
          new PowerTest(context),
          new HapticsTest(context),
          new ImuTest(context),
          new SoundTest(context),
          new MicTest(context),
          new WifiTest(context),
          new RtcTest(context),
          new BatteryInfoTest(context),
      }
    , _current(0)
    , _state(State::WAITING)
    , _test_started_ms(0)
    , _click_pending(false)
    , _long_press_pending(false)
    , _single_click_pending(false)
    , _first_click_ms(0)
    , _power_menu(context, screen)
    , _hold_active(false)
    , _hold_started_ms(0)
    , _menu_interrupted_test(false) {
    for (uint8_t i = 0; i < TEST_COUNT; i++) {
        _outcomes[i] = Outcome::NOT_RUN;
    }
}

HwTestRunner::~HwTestRunner() {
    for (uint8_t i = 0; i < TEST_COUNT; i++) {
        delete _tests[i];
        _tests[i] = nullptr;
    }
}

void HwTestRunner::begin() {
    if (_context.power) {
        _context.power->onButtonClick([this]() { _click_pending = true; });
        _context.power->onButtonLongPress([this]() { _long_press_pending = true; });
    }

    ESP_LOGI(TAG, "Serial commands: n=next r=rerun s=summary wifi <ssid> <pass>");
    startTest(0);
}

void HwTestRunner::loop() {
    if (_context.power) {
        _context.power->updateButton();
    }
    pollSerial();

    bool click = _click_pending;
    bool long_press = _long_press_pending;
    _click_pending = false;
    _long_press_pending = false;

    bool test_wants_button = _state == State::RUNNING && _tests[_current]->wantsButton();
    if (long_press && test_wants_button && _context.haptics) {
        // Buzz at ~1 s so the operator knows the hold registered and lets go
        // well before the 6 s power menu.
        _context.haptics->playEffect(HAPTIC_STRONG_CLICK_100);
    }

    ButtonEvent event = classifyButton(click, long_press);
    if (event != ButtonEvent::NONE) {
        ESP_LOGI(TAG, "Button: %s (%s, %s)", buttonEventName(event),
                 _power_menu.isOpen() ? "power menu"
                                      : (_state == State::RUNNING ? "test running" : "test finished"),
                 _tests[_current]->getName());
    }
    if (holdReachedPowerMenu(long_press)) {
        openPowerMenu();
    }
    if (_power_menu.isOpen()) {
        // Tests and the footer are paused while the overlay is up.
        handlePowerMenu(event);
        delay(1);
        return;
    }

    HardwareTest* test = _tests[_current];

    if (_state == State::RUNNING) {
        if (test->wantsButton()) {
            if (event == ButtonEvent::CLICK) {
                test->onButtonClick();
            } else if (event == ButtonEvent::DOUBLE_CLICK) {
                test->onButtonDoubleClick();
            } else if (event == ButtonEvent::LONG_PRESS) {
                test->onButtonLongPress();
            }
        }

        HardwareTest::Result result = test->update(_screen);
        if (result != HardwareTest::Result::RUNNING) {
            finishTest(result == HardwareTest::Result::PASSED ? Outcome::PASSED
                                                              : Outcome::FAILED);
        }
    } else if (event == ButtonEvent::DOUBLE_CLICK) {
        startTest(_current);
    } else if (event == ButtonEvent::CLICK) {
        advance();
    } else {
        _screen.updateFooter();
    }

    // Yield so IDLE on this core can feed the task watchdog.
    delay(1);
}

HwTestRunner::ButtonEvent HwTestRunner::classifyButton(bool click, bool long_press) {
    uint32_t now = millis();
    bool single_expired =
        _single_click_pending && now - _first_click_ms > DOUBLE_CLICK_WINDOW_MS;

    if (long_press) {
        // A hold always starts with a press, never a click, so any pending
        // single click is stale; drop it rather than acting on it.
        _single_click_pending = false;
        return ButtonEvent::LONG_PRESS;
    }

    if (click) {
        if (_single_click_pending && !single_expired) {
            _single_click_pending = false;
            return ButtonEvent::DOUBLE_CLICK;
        }
        // Start a new window; if an old single was still waiting (the loop was
        // blocked past its window), report it now.
        _single_click_pending = true;
        _first_click_ms = now;
        return single_expired ? ButtonEvent::CLICK : ButtonEvent::NONE;
    }

    if (single_expired) {
        _single_click_pending = false;
        return ButtonEvent::CLICK;
    }
    return ButtonEvent::NONE;
}

bool HwTestRunner::holdReachedPowerMenu(bool long_press) {
    uint32_t now = millis();
    if (long_press) {
        _hold_active = true;
        _hold_started_ms = now - LONG_PRESS_IRQ_MS;
    }
    if (!_hold_active) {
        return false;
    }
    if (!_context.power || !_context.power->isLongPressActive()) {
        _hold_active = false;  // released before the menu hold time
        return false;
    }
    if (_power_menu.isOpen() || now - _hold_started_ms < POWER_MENU_HOLD_MS) {
        return false;
    }
    _hold_active = false;  // one menu per hold
    ESP_LOGI(TAG, "Button: held %lu ms, opening power menu",
             static_cast<unsigned long>(now - _hold_started_ms));
    return true;
}

void HwTestRunner::openPowerMenu() {
    _menu_interrupted_test = _state == State::RUNNING;
    if (_menu_interrupted_test) {
        _tests[_current]->finish();
        ESP_LOGW(TAG, "Power menu interrupted %s", _tests[_current]->getName());
    }
    _single_click_pending = false;
    _power_menu.open();
}

void HwTestRunner::handlePowerMenu(ButtonEvent event) {
    if (event == ButtonEvent::CLICK) {
        _power_menu.selectNext();
        return;
    }
    if (event != ButtonEvent::DOUBLE_CLICK || !_power_menu.confirm()) {
        return;
    }

    // Menu closed (Cancel, or a shutdown the PMIC refused): put the page back.
    if (_menu_interrupted_test) {
        startTest(_current);
    } else {
        _screen.repaint();
    }
}

const char* HwTestRunner::buttonEventName(ButtonEvent event) {
    switch (event) {
        case ButtonEvent::CLICK:
            return "click";
        case ButtonEvent::DOUBLE_CLICK:
            return "double click";
        case ButtonEvent::LONG_PRESS:
            return "hold";
        case ButtonEvent::NONE:
        default:
            return "none";
    }
}

void HwTestRunner::startTest(uint8_t index) {
    _current = index;
    _state = State::RUNNING;
    _test_started_ms = millis();
    _screen.showTest(index, TEST_COUNT - 1, _tests[index]->getName());
    _tests[index]->start(_screen);
}

void HwTestRunner::finishTest(Outcome outcome) {
    _tests[_current]->finish();
    _outcomes[_current] = outcome;
    _state = State::WAITING;
    ESP_LOGI(TAG, "%s: %s after %lu ms", _tests[_current]->getName(), outcomeName(outcome),
             static_cast<unsigned long>(millis() - _test_started_ms));

    _screen.setStatus(outcome == Outcome::PASSED ? TestScreen::Status::DONE
                                                 : TestScreen::Status::FAILED);
    _screen.setFooter(FOOTER_HINT, COLOR_CYAN);

    if (_current == TEST_COUNT - 1) {
        printSummary();
    }
}

void HwTestRunner::advance() {
    uint8_t next = _current + 1;
    if (next >= TEST_COUNT) {
        next = FIRST_CYCLED_TEST;
    }
    startTest(next);
}

void HwTestRunner::pollSerial() {
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            handleSerialCommand(_serial_line);
            _serial_line = "";
            continue;
        }
        if (_serial_line.length() < MAX_SERIAL_LINE) {
            _serial_line += c;
        }
    }
}

void HwTestRunner::handleSerialCommand(String line) {
    line.trim();
    if (line.isEmpty()) {
        return;
    }
    // Never echo a WiFi password to the log.
    ESP_LOGI(TAG, "Serial command: %s", line.startsWith("wifi ") ? "wifi <ssid> <pass>" : line.c_str());

    if (_power_menu.isOpen() && (line == "n" || line == "r")) {
        ESP_LOGW(TAG, "Power menu is open; close it with the button first");
        return;
    }

    if (line == "n") {
        if (_state == State::RUNNING) {
            _tests[_current]->finish();
            _outcomes[_current] = Outcome::SKIPPED;
            ESP_LOGW(TAG, "Skipped %s", _tests[_current]->getName());
        }
        advance();
    } else if (line == "r") {
        if (_state == State::RUNNING) {
            _tests[_current]->finish();
        }
        startTest(_current);
    } else if (line == "s") {
        printSummary();
    } else if (line.startsWith("wifi ")) {
        String args = line.substring(5);
        args.trim();
        int split = args.indexOf(' ');
        String ssid = split < 0 ? args : args.substring(0, split);
        String password = split < 0 ? String("") : args.substring(split + 1);
        bool saved = WifiCredentialStore::save(ssid.c_str(), password.c_str());
        ESP_LOGI(TAG, "WiFi credentials for '%s' %s", ssid.c_str(),
                 saved ? "saved; rerun WIFI test" : "NOT saved");
    } else {
        ESP_LOGW(TAG, "Unknown command '%s' (n, r, s, wifi <ssid> <pass>)", line.c_str());
    }
}

void HwTestRunner::printSummary() const {
    uint8_t passed = 0;
    ESP_LOGI(TAG, "==== Hardware test summary ====");
    for (uint8_t i = 0; i < TEST_COUNT; i++) {
        if (_outcomes[i] == Outcome::PASSED) {
            passed++;
        }
        ESP_LOGI(TAG, "  %02u %-12s %s", i, _tests[i]->getName(), outcomeName(_outcomes[i]));
    }
    ESP_LOGI(TAG, "  %u/%u passed", passed, TEST_COUNT);
}

const char* HwTestRunner::outcomeName(Outcome outcome) {
    switch (outcome) {
        case Outcome::PASSED:
            return "PASS";
        case Outcome::FAILED:
            return "FAIL";
        case Outcome::SKIPPED:
            return "SKIP";
        case Outcome::NOT_RUN:
        default:
            return "-";
    }
}
