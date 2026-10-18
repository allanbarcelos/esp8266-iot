// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

#include <string>
#include <vector>
#include <unity.h>
#include "../test_support.h"

static Logger lgr;
static time_t clockNow = 0;
static Clock testClock{[]() { return clockNow; }};
static const uint32_t E = 1792022400u;   // 2026-10-15 00:00 UTC

void setUp() {
    lgr = Logger();
    resetMocks();
    clockNow = 0;
}
void tearDown() {}

struct Line {
    uint32_t    t;
    std::string text;
};

static std::vector<Line> lines(uint8_t from = 0) {
    std::vector<Line> v;
    lgr.each([](void* ctx, uint32_t t, const char* text) {
        static_cast<std::vector<Line>*>(ctx)->push_back({t, text});
    }, &v, from);
    return v;
}

// A line waits in RAM with its time (seconds since boot without a clock);
// tick() writes it to /log.0 after FLUSH_MS
void test_line_waits_then_goes_to_the_file() {
    setMillis(5000);
    lgr.log("hello %s", "world");
    std::vector<Line> v = lines();
    TEST_ASSERT_EQUAL(1, (int)v.size());
    TEST_ASSERT_EQUAL_UINT32(5, v[0].t);
    TEST_ASSERT_EQUAL_STRING("hello world", v[0].text.c_str());
    TEST_ASSERT_FALSE(LittleFS.exists("/log.0"));

    lgr.tick(Logger::FLUSH_MS - 1);
    TEST_ASSERT_FALSE(LittleFS.exists("/log.0"));
    lgr.tick(Logger::FLUSH_MS);
    TEST_ASSERT_TRUE(LittleFS.exists("/log.0"));
    v = lines();
    TEST_ASSERT_EQUAL(1, (int)v.size());
    TEST_ASSERT_EQUAL_STRING("hello world", v[0].text.c_str());
    // Nothing waiting: nothing written
    lgr.tick(2 * Logger::FLUSH_MS);
    TEST_ASSERT_EQUAL(1, (int)lines().size());
}

// Half of the RAM taken: written at once. The text goes packed
void test_half_full_is_written_and_packed() {
    const char* text = "Configuration saved";
    size_t lineCount = 0;
    while (!LittleFS.exists("/log.0")) {
        lgr.log("%s", text);
        lineCount++;
        lgr.tick(1000);
    }
    const std::string& file = MockFS::files["/log.0"];
    TEST_ASSERT_TRUE(file.size() < lineCount * (5 + strlen(text)));
    TEST_ASSERT_TRUE(file.size() >= Logger::PENDING / 2);
    for (const Line& l : lines()) TEST_ASSERT_EQUAL_STRING(text, l.text.c_str());
}

// Once the clock is set, lines get Unix time: those that waited from before
// are turned on the way to the file
void test_clock_gives_unix_time() {
    lgr.setClock(&testClock);
    setMillis(5000);
    lgr.log("early");
    lgr.flush(5000);                   // the clock not set yet: as it was
    setMillis(10000);
    lgr.log("before");
    clockNow = E;
    setMillis(20000);
    lgr.log("after");
    std::vector<Line> v = lines();
    TEST_ASSERT_EQUAL_UINT32(5, v[0].t);
    TEST_ASSERT_EQUAL_UINT32(10, v[1].t);
    TEST_ASSERT_EQUAL_UINT32(E, v[2].t);
    lgr.flush(20000);
    v = lines();
    TEST_ASSERT_EQUAL_UINT32(5, v[0].t);       // written before the clock
    TEST_ASSERT_EQUAL_UINT32(E - 10, v[1].t);
    TEST_ASSERT_EQUAL_UINT32(E, v[2].t);
}

// Full, /log.0 becomes /log.1 and that one /log.2; the oldest goes. Read
// oldest first; from = 2 is the current file and RAM
void test_files_rotate() {
    char name[16];
    for (int i = 0; i < 2000; i++) {
        lgr.log("line %d with some text", i);
        lgr.tick(1000);
    }
    TEST_ASSERT_TRUE(LittleFS.exists("/log.2"));
    for (const char* p : Logger::PATHS) TEST_ASSERT_TRUE(MockFS::files[p].size() <= Logger::FILE_MAX);
    std::vector<Line> v = lines();
    TEST_ASSERT_TRUE(v.size() < 2000);
    std::snprintf(name, sizeof(name), "line %d", 1999);
    TEST_ASSERT_EQUAL_STRING("line 1999 with some text", v.back().text.c_str());
    int first = std::atoi(v.front().text.c_str() + 5);
    for (size_t i = 0; i < v.size(); i++) {
        std::snprintf(name, sizeof(name), "line %d ", first + (int)i);
        TEST_ASSERT_EQUAL(0, (int)v[i].text.find(name));
    }
    std::vector<Line> current = lines(2);
    TEST_ASSERT_TRUE(current.size() < v.size());
    TEST_ASSERT_EQUAL_STRING(v.back().text.c_str(), current.back().text.c_str());
}

// No tick for a while: a full RAM drops its oldest lines
void test_full_ram_drops_the_oldest() {
    for (int i = 0; i < 200; i++) lgr.log("line %d", i);
    std::vector<Line> v = lines();
    TEST_ASSERT_TRUE(v.size() < 200);
    TEST_ASSERT_EQUAL_STRING("line 199", v.back().text.c_str());
    TEST_ASSERT_FALSE(LittleFS.exists("/log.0"));
}

// Bytes from 0x80 up are the pairs, '\n' ends a line: both go as '?'; a
// long line is cut
void test_text_is_kept_apart_from_the_packing() {
    lgr.log("SSID Caf\xc3\xa9\nnext");
    TEST_ASSERT_EQUAL_STRING("SSID Caf??", lines()[0].text.substr(0, 10).c_str());
    TEST_ASSERT_EQUAL_STRING("SSID Caf???next", lines()[0].text.c_str());
    std::string big(500, 'A');
    lgr.log("%s", big.c_str());
    TEST_ASSERT_EQUAL(LOG_LINE_SIZE - 1, (int)lines()[1].text.size());
}

// Without the file system the lines go (they were on Serial)
void test_no_file_system() {
    lgr.flush();                       // nothing waiting: no file
    TEST_ASSERT_FALSE(LittleFS.exists("/log.0"));
    lgr.log("lost");
    MockFS::failWriteOpen = true;
    lgr.flush();
    TEST_ASSERT_EQUAL(0, (int)lines().size());
}

// A damaged file: a line longer than a line is cut; a cut time ends it
void test_damaged_file() {
    std::string f("\x01\x00\x00\x00", 4);
    f += std::string(300, 'B') + "\n";
    f += std::string("\x02\x00", 2);
    MockFS::put("/log.1", f);
    MockFS::put("/log.0", std::string("\x03\x00\x00\x00", 4) + "no end");
    std::vector<Line> v = lines();
    TEST_ASSERT_EQUAL(2, (int)v.size());
    TEST_ASSERT_EQUAL_UINT32(1, v[0].t);
    TEST_ASSERT_EQUAL(LOG_LINE_SIZE - 1, (int)v[0].text.size());
    TEST_ASSERT_EQUAL_STRING("no end", v[1].text.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_line_waits_then_goes_to_the_file);
    RUN_TEST(test_half_full_is_written_and_packed);
    RUN_TEST(test_clock_gives_unix_time);
    RUN_TEST(test_files_rotate);
    RUN_TEST(test_full_ram_drops_the_oldest);
    RUN_TEST(test_text_is_kept_apart_from_the_packing);
    RUN_TEST(test_no_file_system);
    RUN_TEST(test_damaged_file);
    return UNITY_END();
}
