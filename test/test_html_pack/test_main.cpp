// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

#include <cstring>
#include <string>
#include <unity.h>
#include "../test_support.h"
#include "HtmlPack.h"

// html::encode at run time: the compiler runs the same code for every H()
// text; the pages (test_web_service) cover the expansion in WebService::_p.

void setUp()    { resetMocks(); }
void tearDown() {}

// The same expansion as WebService::_put
static void expand(uint8_t c, std::string& out) {
    if (c < html::CODE) {
        out += (char)c;
        return;
    }
    expand(HTML_PAIRS[c - html::CODE][0], out);
    expand(HTML_PAIRS[c - html::CODE][1], out);
}

static std::string unpack(const html::Buf& b) {
    std::string out;
    for (size_t i = 0; b.c[i]; i++) expand((uint8_t)b.c[i], out);
    return out;
}

static void roundTrip(const std::string& t) {
    html::Buf b = html::encode(t.c_str());
    TEST_ASSERT_EQUAL_UINT32(strlen(b.c) + 1, b.len);
    TEST_ASSERT_TRUE(b.len <= t.size() + 1);
    TEST_ASSERT_EQUAL_STRING(t.c_str(), unpack(b).c_str());
}

void test_packs_and_expands_back() {
    roundTrip("");
    roundTrip("<label class='sw'><input type='checkbox' role='switch' id='");
    roundTrip("</section></main><footer><span>");
    roundTrip("required inputmode='numeric' pattern='[0-9]{6}' minlength='6' maxlength='6' autocomplete='one-time-code'");
    // The first pair alone, at the end, before another byte, and whole
    const char a = (char)HTML_PAIRS[0][0], z = (char)HTML_PAIRS[0][1];
    roundTrip(std::string(1, a));
    roundTrip(std::string(1, a) + (char)(z + 1));
    roundTrip(std::string(1, a) + z);
    TEST_ASSERT_EQUAL_UINT32(2, html::encode((std::string(1, a) + z).c_str()).len);
    // Page markup is what the pairs were picked from: it shrinks
    TEST_ASSERT_TRUE(html::encode("<section class='c'><h2>").len < 12);
}

void test_every_pair_expands_to_ascii() {
    TEST_ASSERT_TRUE(HTML_PAIR_COUNT > 0 && HTML_PAIR_COUNT <= 128);
    for (uint8_t k = 0; k < HTML_PAIR_COUNT; k++) {
        std::string s;
        expand((uint8_t)(html::CODE + k), s);
        TEST_ASSERT_TRUE(s.size() >= 2);
        for (char c : s) TEST_ASSERT_TRUE(c > 0);
        roundTrip(s);
    }
}

// Not ASCII, or too long: no text (H() stops the build on it)
void test_refuses_what_it_cannot_pack() {
    TEST_ASSERT_EQUAL_UINT32(0, html::encode("Fran\xc3\xa7" "ais").len);
    TEST_ASSERT_EQUAL_UINT32(0, html::encode("\x80").len);
    std::string longest(html::MAX - 1, 'z');
    TEST_ASSERT_EQUAL_UINT32(0, html::encode((longest + "z").c_str()).len);
    roundTrip(longest);
}

// H() and HTML_TEXT give the packed bytes; a plain PGM_P is an Html too
HTML_TEXT(_NAMED, "<section class='c'><h2>");

void test_macros() {
    Html h = H("<section class='c'><h2>");
    TEST_ASSERT_EQUAL_STRING(html::encode("<section class='c'><h2>").c, h.p);
    TEST_ASSERT_EQUAL_STRING(h.p, _NAMED().p);
    const char* plain = "<b>";
    Html p = plain;
    TEST_ASSERT_EQUAL_PTR(plain, p.p);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_packs_and_expands_back);
    RUN_TEST(test_every_pair_expands_to_ascii);
    RUN_TEST(test_refuses_what_it_cannot_pack);
    RUN_TEST(test_macros);
    return UNITY_END();
}
