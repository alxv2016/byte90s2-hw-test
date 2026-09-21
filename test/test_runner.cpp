/**
 * @file test_runner.cpp
 * @brief Minimal PlatformIO Unity runner placeholder.
 */

#include <Arduino.h>
#include <unity.h>

static void test_runner_placeholder(void) {
    TEST_ASSERT_TRUE(true);
}

void setUp(void) {
}

void tearDown(void) {
}

void setup() {
    delay(1000);
    Serial.begin(115200);

    UNITY_BEGIN();
    RUN_TEST(test_runner_placeholder);
    UNITY_END();
}

void loop() {
    delay(100);
}
