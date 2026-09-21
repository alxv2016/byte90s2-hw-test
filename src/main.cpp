/**
 * main.cpp
 *
 * BYTE-90 hardware test firmware entry point.
 */

#include "RuntimeDiagnostics.h"
#include "hwtest/HwTestApp.h"

#include <Arduino.h>

static HwTestApp g_app;

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
    (void)xTask;
    RuntimeDiagnostics::instance().reportStackOverflow(pcTaskName);
}

void setup() { g_app.begin(); }

void loop() { g_app.loop(); }
