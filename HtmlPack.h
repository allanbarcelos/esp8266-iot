// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

#pragma once
#include <Arduino.h>
#include "HtmlPairs.h"

// The panel's fixed HTML, packed by the compiler. H("...") is a PSTR whose
// text has gone through the pairs of HtmlPairs.h, in order: each turns its
// two bytes into one, 0x80 + its index (a half can be an earlier pair).
// The text must be ASCII, so a byte from 0x80 up is always a pair; the
// device expands it as the page goes out (WebService::_p). Equal texts are
// still merged by the linker, as with PSTR.
//
// The packed text has its own type, Html: it goes to _p and to the helpers
// that only write it, never to strcmp_P, printf or a String (it does not
// turn into a PGM_P). A plain PGM_P turns into an Html: ASCII expands to
// itself.

struct Html {
    PGM_P p;
    constexpr Html(PGM_P s) : p(s) {}  // cppcheck-suppress noExplicitConstructor ; any ASCII PGM_P is HTML
};

namespace html {

constexpr uint8_t CODE = 0x80;
constexpr size_t  MAX  = 320;  // the longest text, with its '\0'

struct Buf {
    char   c[MAX];
    size_t len;  // with the '\0'; 0 = not ASCII or too long
};

// The packing, run by the compiler (and by the tests, at run time)
constexpr Buf encode(const char* s) {
    Buf b{};
    size_t n = 0;
    for (; s[n]; n++) {
        if (n + 1 == MAX || (uint8_t)s[n] >= CODE) return Buf{};
        b.c[n] = s[n];
    }
    for (uint8_t k = 0; k < HTML_PAIR_COUNT; k++) {
        const char a = (char)HTML_PAIRS[k][0], z = (char)HTML_PAIRS[k][1];
        size_t o = 0;
        for (size_t i = 0; i < n; i++, o++) {
            if (b.c[i] == a && i + 1 < n && b.c[i + 1] == z) {
                b.c[o] = (char)(CODE + k);
                i++;
            } else {
                b.c[o] = b.c[i];
            }
        }
        n = o;
    }
    b.c[n] = '\0';
    b.len = n + 1;
    return b;
}

#if defined(ARDUINO_ARCH_ESP8266)
// Exactly the packed bytes, for flash
template <size_t N> struct Str { char c[N]; };
template <size_t N> constexpr Str<N> cut(const Buf& b) {
    Str<N> s{};
    for (size_t i = 0; i < N; i++) s.c[i] = b.c[i];
    return s;
}
#endif

}  // namespace html

#if defined(ARDUINO_ARCH_ESP8266)
// PSTR's section: flash, and equal strings merged by the linker
#define HTML_SECTION_ __attribute__((section("\".irom0.pstr." __FILE__ "." __STRINGIZE(__LINE__) "." \
                                              __STRINGIZE(__COUNTER__) "\", \"aSM\", @progbits, 1 #")))
#define HTML_STORE_(buf, str)                                                       \
    static_assert(buf.len, "H(): the text must be ASCII and shorter than html::MAX"); \
    static const html::Str<buf.len> str HTML_SECTION_ = html::cut<buf.len>(buf)
#define HTML_AT_(buf, str) str.c
#else
#define HTML_STORE_(buf, str) \
    static_assert(buf.len, "H(): the text must be ASCII and shorter than html::MAX")
#define HTML_AT_(buf, str) buf.c
#endif

// A packed text, where PSTR would go
#define H(s)                                                     \
    (__extension__({                                             \
        static constexpr html::Buf _hb = html::encode(s);        \
        HTML_STORE_(_hb, _hs);                                   \
        Html(HTML_AT_(_hb, _hs));                                \
    }))

// A packed text with a name, at file scope: name() gives it (a function,
// not an Html object, which would take 4 bytes of RAM)
#define HTML_TEXT(name, s)                                       \
    static constexpr html::Buf name##_buf = html::encode(s);     \
    HTML_STORE_(name##_buf, name##_str);                         \
    static inline Html name() { return Html(HTML_AT_(name##_buf, name##_str)); }
