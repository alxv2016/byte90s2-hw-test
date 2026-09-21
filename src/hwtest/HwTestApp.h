/**
 * HwTestApp.h
 *
 * Hardware test firmware host: brings up every BYTE-90 peripheral, plays the
 * DOS boot animation and splash, then hands control to HwTestRunner, whose
 * first test plays the startup GIF.
 */

#pragma once

#include "hwtest/HwTestContext.h"
#include "hwtest/TestScreen.h"

class HwTestRunner;

class HwTestApp {
public:
    HwTestApp();
    ~HwTestApp();

    void begin();
    void loop();

private:
    void initializeHardware();
    void runBootAnimation();
    void showSplash();

    HwTestContext _context;
    TestScreen _screen;
    HwTestRunner* _runner;
};
