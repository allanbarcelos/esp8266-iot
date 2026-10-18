// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

#pragma once
#include <Arduino.h>

// Something done once a day at a local time (the update check, the
// restart), on the days of the week in a mask (bit 0 = Sunday). Due on the
// first call within WINDOW minutes after the time, so a loop busy with a
// TLS handshake does not skip it; once per occurrence. The first WINDOW
// minutes after boot are never due: a restart at the set time does not
// repeat on the next boot. Without the clock, every 24 h of uptime instead.
class Daily {
public:
    static constexpr uint8_t       EVERY_DAY = 0x7F;
    static constexpr uint16_t      WINDOW    = 15;   // minutes
    static constexpr unsigned long DAY_MS    = 86400000UL;

    // utc: Unix time (0 = the clock is not set); tz: local offset, minutes;
    // at: minute of the day; now: millis()
    bool due(uint32_t utc, int16_t tz, uint16_t at, uint8_t days, unsigned long now) {
        if (!days || now < WINDOW * 60000UL) return false;
        if (!utc) {
            if (now - _last < DAY_MS) return false;
            _last = now;
            return true;
        }
        // Minutes since the first occurrence: 32 bits are good until 2106
        uint32_t m = (utc + (int32_t)tz * 60) / 60 - at;
        uint32_t day = m / 1440;   // the occurrence's day, counted from 1970-01-01 (a Thursday)
        if (m % 1440 >= WINDOW || day == _done || !(days >> ((day + 4) % 7) & 1)) return false;
        _done = day;
        _last = now;
        return true;
    }

private:
    uint32_t      _done = 0;
    unsigned long _last = 0;
};
