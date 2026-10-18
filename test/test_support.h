// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

#pragma once
// Helpers shared by the native test suites.
#include <lwip/udp.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <Updater.h>
#include <cstring>
#include "Logger.h"
#include "CloudflareCA.h"

// malloc fails once for the requested size (linked with -Wl,--wrap=malloc
// in the native env). Only that exact size fails: libc and Unity keep working.
namespace mock { inline size_t failMallocSize = 0; }
extern "C" void* __real_malloc(size_t n);
extern "C" void* __wrap_malloc(size_t n) {
    if (mock::failMallocSize != 0 && n == mock::failMallocSize) {
        mock::failMallocSize = 0;
        return nullptr;
    }
    return __real_malloc(n);
}

// Restores every global mock to its initial state
inline void resetMocks() {
    setMillis(0);
    MockHTTP::reset();
    MockTLS::reset();
    MockServer::reset();
    MockFS::reset();
    WiFi.reset();
    ESP.reset();
    Update.reset();
    MockUDP::reset();
    mock::configTimeCalls = 0;
    mock::failMallocSize = 0;
    Serial.last.clear();
    Serial.printfCalls = 0;
    mock::ntpServer1.clear();
    mock::resetPins();
    mock::resetBus();
}

// How many lines of the log (its files and RAM) contain the text ("": all)
inline int logCount(const Logger& log, const char* needle = "") {
    struct Find { const char* needle; int n; } f{needle, 0};
    log.each([](void* ctx, uint32_t, const char* text) {
        Find& f = *static_cast<Find*>(ctx);
        f.n += std::strstr(text, f.needle) != nullptr;
    }, &f);
    return f.n;
}

inline bool logHas(const Logger& log, const char* needle) { return logCount(log, needle) > 0; }

inline bool contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

// The connection trusts exactly the anchors in CloudflareCA.h, all complete
inline bool cloudflareTrust(const MockTLS::Trust& t) {
    return t.complete && t.count == CLOUDFLARE_ROOTS
        && t.bytes == sizeof(CLOUDFLARE_TA) - 7 * CLOUDFLARE_ROOTS;
}
