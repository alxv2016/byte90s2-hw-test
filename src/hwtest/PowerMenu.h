/**
 * PowerMenu.h
 *
 * Software power menu, opened by a 6 s power-key hold (HwTestRunner). It
 * replaces the AXP2101's hardware long-press power-off, which HwTestApp
 * disables.
 *
 * Options: Cancel, Restart, Shutdown. Opens on Cancel. A single click moves
 * the selection down (wrapping); a double click selects it.
 *
 *   Cancel    closes the menu; the caller restores the page underneath
 *   Restart   ESP restart; the PMIC stays on
 *   Shutdown  AXP2101 power-off; press the key ~0.5 s to power back on
 */

#pragma once

#include "hwtest/HwTestContext.h"
#include "hwtest/TestScreen.h"

class PowerMenu {
public:
    PowerMenu(HwTestContext& context, TestScreen& screen);

    void open();
    bool isOpen() const { return _open; }

    /**
     * @brief Move the selection to the next option
     */
    void selectNext();

    /**
     * @brief Act on the selected option
     *
     * Restart does not return. Shutdown only returns if the PMIC kept power
     * (for example USB holding the rail), after showing a message.
     *
     * @return true when the menu has closed and the caller should restore
     *         the screen
     */
    bool confirm();

private:
    enum class Option : uint8_t {
        CANCEL,
        RESTART,
        SHUTDOWN,
        COUNT,
    };

    void draw();
    void drawOption(Option option);
    void showMessage(const char* line1, const char* line2, uint16_t color);
    void restart();
    void shutdown();

    HwTestContext& _context;
    TestScreen& _screen;
    bool _open;
    Option _selected;
};
