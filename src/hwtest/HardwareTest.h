/**
 * HardwareTest.h
 *
 * Contract for one hardware test page. The runner calls start() once, then
 * update() every loop until it returns PASSED or FAILED, then finish().
 * update() must not block for more than a few hundred milliseconds so the
 * button and serial stay responsive.
 */

#pragma once

#include "hwtest/HwTestContext.h"
#include "hwtest/TestScreen.h"

class HardwareTest {
public:
    enum class Result : uint8_t {
        RUNNING,
        PASSED,
        FAILED,
    };

    explicit HardwareTest(HwTestContext& context) : _context(context) {}
    virtual ~HardwareTest() = default;

    virtual const char* getName() const = 0;
    virtual void start(TestScreen& screen) = 0;
    virtual Result update(TestScreen& screen) = 0;

    /**
     * @brief Release anything start() acquired; called once after update ends
     */
    virtual void finish() {}

    /**
     * @brief true if button events belong to the test while it is running
     */
    virtual bool wantsButton() const { return false; }
    virtual void onButtonClick() {}
    virtual void onButtonDoubleClick() {}
    virtual void onButtonLongPress() {}

protected:
    HwTestContext& _context;
};
