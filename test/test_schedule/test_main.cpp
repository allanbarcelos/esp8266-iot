// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

#include <unity.h>
#include "../test_support.h"
#include "Schedule.h"

void setUp()    { resetMocks(); }
void tearDown() {}

static const uint32_t THU = 1792022400u;   // 2026-10-15 00:00 UTC, a Thursday
static const uint32_t MIN = 60, HOUR = 3600, DAY = 86400;
static const unsigned long UP = 3600000UL;  // an hour of uptime: past the boot guard
static const uint16_t AT9 = 9 * 60;

void test_once_in_the_window_after_the_time() {
    Daily d;
    TEST_ASSERT_FALSE(d.due(THU + 9 * HOUR - MIN, 0, AT9, Daily::EVERY_DAY, UP));   // 08:59
    TEST_ASSERT_TRUE(d.due(THU + 9 * HOUR, 0, AT9, Daily::EVERY_DAY, UP));          // 09:00
    TEST_ASSERT_FALSE(d.due(THU + 9 * HOUR + MIN, 0, AT9, Daily::EVERY_DAY, UP));   // done today
    // A busy loop: the first call within the window still counts
    Daily late;
    TEST_ASSERT_TRUE(late.due(THU + 9 * HOUR + 14 * MIN, 0, AT9, Daily::EVERY_DAY, UP));
    Daily missed;
    TEST_ASSERT_FALSE(missed.due(THU + 9 * HOUR + 15 * MIN, 0, AT9, Daily::EVERY_DAY, UP));
    // The next day, again
    TEST_ASSERT_TRUE(d.due(THU + DAY + 9 * HOUR + 2 * MIN, 0, AT9, Daily::EVERY_DAY, UP));
}

// Local time: UTC-3 at 09:00 is 12:00 UTC; UTC+5:30 is 03:30 UTC
void test_local_time() {
    Daily brt;
    TEST_ASSERT_FALSE(brt.due(THU + 9 * HOUR, -180, AT9, Daily::EVERY_DAY, UP));
    TEST_ASSERT_TRUE(brt.due(THU + 12 * HOUR, -180, AT9, Daily::EVERY_DAY, UP));
    Daily ist;
    TEST_ASSERT_TRUE(ist.due(THU + 3 * HOUR + 30 * MIN, 330, AT9, Daily::EVERY_DAY, UP));
}

// Bit 0 = Sunday: Thursday is bit 4
void test_days_of_the_week() {
    Daily d;
    const uint8_t weekend = 0x41;   // Sunday and Saturday
    TEST_ASSERT_FALSE(d.due(THU, 0, 0, weekend, UP));             // Thursday
    TEST_ASSERT_FALSE(d.due(THU + DAY, 0, 0, weekend, UP));       // Friday
    TEST_ASSERT_TRUE(d.due(THU + 2 * DAY, 0, 0, weekend, UP));    // Saturday
    TEST_ASSERT_TRUE(d.due(THU + 3 * DAY, 0, 0, weekend, UP));    // Sunday
    TEST_ASSERT_FALSE(d.due(THU + 4 * DAY, 0, 0, weekend, UP));   // Monday
    Daily thu;
    TEST_ASSERT_TRUE(thu.due(THU, 0, 0, 1 << 4, UP));
    // No day: never, with or without the clock
    Daily off;
    TEST_ASSERT_FALSE(off.due(THU, 0, 0, 0, UP));
    TEST_ASSERT_FALSE(off.due(0, 0, 0, 0, 2 * Daily::DAY_MS));
}

// Near midnight the window crosses into the next day: still one occurrence,
// counted on the day of the time (23:55 Thursday)
void test_window_across_midnight() {
    Daily d;
    const uint16_t at = 23 * 60 + 55;
    const uint8_t thursday = 1 << 4;
    TEST_ASSERT_TRUE(d.due(THU + 23 * HOUR + 56 * MIN, 0, at, thursday, UP));
    TEST_ASSERT_FALSE(d.due(THU + DAY + 5 * MIN, 0, at, thursday, UP));
    Daily late;
    TEST_ASSERT_TRUE(late.due(THU + DAY + 5 * MIN, 0, at, thursday, UP));   // Thursday's, on Friday
}

// The first minutes after boot are never due: a restart at the time does not repeat
void test_not_right_after_boot() {
    Daily d;
    const unsigned long guard = Daily::WINDOW * 60000UL;
    TEST_ASSERT_FALSE(d.due(THU + 9 * HOUR, 0, AT9, Daily::EVERY_DAY, guard - 1));
    TEST_ASSERT_TRUE(d.due(THU + 9 * HOUR + 1, 0, AT9, Daily::EVERY_DAY, guard));
}

// Without the clock: every 24 h of uptime
void test_without_the_clock() {
    Daily d;
    TEST_ASSERT_FALSE(d.due(0, 0, AT9, Daily::EVERY_DAY, Daily::DAY_MS - 1));
    TEST_ASSERT_TRUE(d.due(0, 0, AT9, Daily::EVERY_DAY, Daily::DAY_MS));
    TEST_ASSERT_FALSE(d.due(0, 0, AT9, Daily::EVERY_DAY, 2 * Daily::DAY_MS - 1));
    TEST_ASSERT_TRUE(d.due(0, 0, AT9, Daily::EVERY_DAY, 2 * Daily::DAY_MS));
    // A run by the clock counts too
    Daily c;
    TEST_ASSERT_TRUE(c.due(THU + 9 * HOUR, 0, AT9, Daily::EVERY_DAY, UP));
    TEST_ASSERT_FALSE(c.due(0, 0, AT9, Daily::EVERY_DAY, UP + Daily::DAY_MS - 1));
    TEST_ASSERT_TRUE(c.due(0, 0, AT9, Daily::EVERY_DAY, UP + Daily::DAY_MS));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_once_in_the_window_after_the_time);
    RUN_TEST(test_local_time);
    RUN_TEST(test_days_of_the_week);
    RUN_TEST(test_window_across_midnight);
    RUN_TEST(test_not_right_after_boot);
    RUN_TEST(test_without_the_clock);
    return UNITY_END();
}
