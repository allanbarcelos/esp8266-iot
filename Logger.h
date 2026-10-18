// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <stdarg.h>
#include "Clock.h"
#include "LogPairs.h"

#define LOG_LINE_SIZE 128

// The log. Each line goes to Serial at once and waits in RAM; tick() writes
// the waiting lines to LittleFS in one go (flash wears with every write):
// every FLUSH_MS, or when half the RAM is taken, and before a restart. A
// line is never written from log() itself: it is called from the Wi-Fi
// events too, where flash work does not belong. A full RAM drops its oldest.
//
// A line is stored as its time (4 bytes: Unix time, or seconds since boot
// while the clock is not set; turned into Unix time on the way to the file
// once it is) and its text packed with LogPairs.h, up to '\n'. The current
// file, /log.0, takes about one LittleFS block; full, it becomes /log.1 and
// that one /log.2, whose lines go: the log never takes more than 3 blocks.
// A kept file whose newest line is more than a day old goes too (KEEP_S).
class Logger {
public:
    static constexpr size_t        PENDING  = 512;      // bytes of lines waiting in RAM
    static constexpr size_t        FILE_MAX = 4000;     // a LittleFS block, 4096, with room to spare
    static constexpr unsigned long FLUSH_MS = 600000UL; // 10 min
    static constexpr uint32_t      KEEP_S   = 86400;    // a kept file, at most a day
    static constexpr const char*   PATHS[]  = {"/log.2", "/log.1", "/log.0"};   // oldest first

    using LineFn = void (*)(void* ctx, uint32_t time, const char* text);

    void setClock(const Clock* clock) { _clock = clock; }

    // fmt lives in flash (PSTR): on the ESP8266 plain string literals take
    // RAM, and the heap is what the OTA TLS needs. vsnprintf_P reads RAM too.
    void log(PGM_P fmt, ...) {
        char msg[LOG_LINE_SIZE];
        va_list args;
        va_start(args, fmt);
        vsnprintf_P(msg, sizeof(msg), fmt, args);
        va_end(args);
        {
            char line[LOG_LINE_SIZE + 24];   // "[4294967s] " and the text
            snprintf_P(line, sizeof(line), PSTR("[%lus] %s"), millis() / 1000, msg);
            Serial.println(line);
        }

        // Bytes from 0x80 up are the pairs: a name in UTF-8 goes as '?'
        size_t n = 0;
        for (; msg[n]; n++)
            if ((uint8_t)msg[n] >= 0x80 || msg[n] == '\n') msg[n] = '?';
        n = _pack(msg, n);
        while (_used + 5 + n > PENDING) _dropOldest();
        uint32_t t = _stamp();
        memcpy(_pending + _used, &t, 4);
        memcpy(_pending + _used + 4, msg, n);
        _pending[_used + 4 + n] = '\n';
        _used += 5 + n;
    }

    void tick(unsigned long now) {
        if (_used >= PENDING / 2 || now - _flushed >= FLUSH_MS) flush(now);
    }

    // Writes what waits to /log.0 (rotating it when full), after the kept
    // files older than a day go. Without the file system the lines go: they
    // are on Serial.
    void flush(unsigned long now = millis()) {
        _flushed = now;
        bool synced = _clock && _clock->synced();
        if (synced) {
            // /log.1 old: /log.2 is older still
            uint32_t at = (uint32_t)_clock->now();
            for (uint8_t k = 2; k-- > 0;) {
                uint32_t newest = 0;
                each([](void* ctx, uint32_t t, const char*) { *static_cast<uint32_t*>(ctx) = t; }, &newest, k, k + 1);
                if (newest >= Clock::MIN_VALID_EPOCH && at - newest > KEEP_S)
                    for (uint8_t j = 0; j <= k; j++) LittleFS.remove(PATHS[j]);
            }
        }
        if (!_used) return;
        if (synced) {
            // Since boot until now: from here on they can have the real time
            uint32_t boot = (uint32_t)_clock->now() - now / 1000;
            for (size_t i = 0; i < _used; i = _next(i)) {
                uint32_t t;
                memcpy(&t, _pending + i, 4);
                if (t < Clock::MIN_VALID_EPOCH) {
                    t += boot;
                    memcpy(_pending + i, &t, 4);
                }
            }
        }
        File f = LittleFS.open(PATHS[2], "a");
        if (f && f.size() + _used > FILE_MAX) {
            f.close();
            LittleFS.remove(PATHS[0]);
            LittleFS.rename(PATHS[1], PATHS[0]);
            LittleFS.rename(PATHS[2], PATHS[1]);
            f = LittleFS.open(PATHS[2], "w");
        }
        if (f) {
            f.write(_pending, _used);
            f.close();
        }
        _used = 0;
    }

    // Every line, oldest first: the files (PATHS from `from`), then RAM (4th,
    // up to `to`). The time as stored: the panel's script writes it as a date.
    void each(LineFn fn, void* ctx, uint8_t from = 0, uint8_t to = 4) const {
        for (uint8_t k = from; k < to; k++) {
            Source src{};
            if (k < 3) {
                src.f = LittleFS.open(PATHS[k], "r");
                if (!src.f) continue;
            } else {
                src.p = _pending;
                src.n = _used;
            }
            char text[LOG_LINE_SIZE];
            for (;;) {
                uint32_t t = 0;
                int c = 0;
                for (uint8_t b = 0; b < 4 && (c = src.get()) >= 0; b++) t |= (uint32_t)c << (8 * b);
                if (c < 0) break;
                size_t n = 0;
                while ((c = src.get()) >= 0 && c != '\n') _expand((uint8_t)c, text, n);
                text[n] = '\0';
                fn(ctx, t, text);
            }
            src.f.close();
        }
    }

private:
    // A file, or the lines in RAM
    struct Source {
        File           f;
        const uint8_t* p = nullptr;
        size_t         i = 0, n = 0;
        int get() { return p ? (i < n ? p[i++] : -1) : f.read(); }
    };

    const Clock*  _clock   = nullptr;
    uint8_t       _pending[PENDING] = {};
    size_t        _used    = 0;
    unsigned long _flushed = 0;

    uint32_t _stamp() const {
        return _clock && _clock->synced() ? (uint32_t)_clock->now() : millis() / 1000;
    }

    // The line after the one at i
    size_t _next(size_t i) const {
        i += 4;
        while (_pending[i] != '\n') i++;
        return i + 1;
    }

    void _dropOldest() {
        size_t k = _next(0);
        memmove(_pending, _pending + k, _used - k);
        _used -= k;
    }

    // In place, each pair of LogPairs.h in order (see tools/html_pairs.py)
    static size_t _pack(char* s, size_t n) {
        for (uint8_t k = 0; k < LOG_PAIR_COUNT; k++) {
            const char a = (char)pgm_read_byte(&LOG_PAIRS[k][0]), z = (char)pgm_read_byte(&LOG_PAIRS[k][1]);
            size_t o = 0;
            for (size_t i = 0; i < n; i++, o++) {
                if (s[i] == a && i + 1 < n && s[i + 1] == z) {
                    s[o] = (char)(0x80 + k);
                    i++;
                } else {
                    s[o] = s[i];
                }
            }
            n = o;
        }
        return n;
    }

    static void _expand(uint8_t c, char* out, size_t& n) {
        if (c < 0x80) {
            if (n < LOG_LINE_SIZE - 1) out[n++] = (char)c;
            return;
        }
        const uint8_t* pair = LOG_PAIRS[c & 0x7F];
        _expand(pgm_read_byte(pair), out, n);
        _expand(pgm_read_byte(pair + 1), out, n);
    }
};
