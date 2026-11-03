// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

#pragma once
#include <ESP8266WiFi.h>
#include "Config.h"
#include "Logger.h"
#include "Clock.h"
#include "CloudflareApi.h"
#include "WiFiService.h"

// Keeps the Cloudflare A record pointing at the public IP.
//
// The IP comes from PublicIPResolver (Config::publicIP). The one already
// synced is kept in memory: we do not rely on the local DNS resolver (which
// caches by TTL and returns Cloudflare IPs for proxied records). After a
// boot, the first known IP always sends a PATCH (idempotent); a failure
// retries every RETRY_INTERVAL.
class CloudflareDNS {
public:
    CloudflareDNS(Config& cfg, Logger& log, WiFiService& wifi, Clock& clock)
        : _cfg(cfg), _log(log), _wifi(wifi), _clock(clock) {
        _syncedIP[0] = '\0';
    }

    // A failure tries again after a minute, then waits twice as long each
    // time, up to RETRY_MAX: a record deleted or a token without DNS Edit
    // would otherwise take the single TLS session (and close the relay)
    // every minute, for good. After a success a new IP goes at once.
    void tick(unsigned long now) {
        if (!due(now)) return;
        _wait = !_attempted ? RETRY_INTERVAL : _wait < RETRY_MAX ? _wait * 2 : RETRY_MAX;
        _attempted   = true;
        _lastAttempt = now;
        _sync();
    }

    // The next tick call will use the network (TLS)
    bool due(unsigned long now) const {
        return configured() && _cfg.publicIP[0] && strcmp(_cfg.publicIP, _syncedIP) != 0 &&
               (!_attempted || now - _lastAttempt >= _wait) &&
               _wifi.isConnected() && _clock.synced();  // TLS needs a valid clock
    }

    bool configured() const {
        return _cfg.cf_token[0] && _cfg.cf_zone[0] && _cfg.cf_record[0];
    }

    const char* syncedIP() const { return _syncedIP; }

    // Another record: the next tick sends the IP again, whatever was synced
    void resync() {
        _syncedIP[0] = 0;
        _attempted = false;
    }

    static constexpr unsigned long RETRY_INTERVAL = 60000UL;
    static constexpr unsigned long RETRY_MAX      = 16 * RETRY_INTERVAL;

private:
    Config&       _cfg;
    Logger&       _log;
    WiFiService&  _wifi;
    Clock&        _clock;
    unsigned long _lastAttempt = 0;
    bool          _attempted   = false;   // failed since the last success
    unsigned long _wait        = RETRY_INTERVAL;
    char          _syncedIP[16];

    void _sync() {
        char ip[16];
        strlcpy(ip, _cfg.publicIP, sizeof(ip));
        _log.log(LOGP("Updating DNS to %s"), ip);
        char path[sizeof("/client/v4/zones//dns_records/") + sizeof(_cfg.cf_zone) + sizeof(_cfg.cf_record)];
        char payload[sizeof("{\"content\":\"\"}") + sizeof(_cfg.publicIP)];
        snprintf_P(path, sizeof(path), PSTR("/client/v4/zones/%s/dns_records/%s"), _cfg.cf_zone, _cfg.cf_record);
        snprintf_P(payload, sizeof(payload), PSTR("{\"content\":\"%s\"}"), ip);
        cfapi::Answer a;
        char why[64];
        if (!cfapi::call(_cfg, _clock, "PATCH", path, payload, a, why)) {
            _log.log(LOGP("DNS: %s"), why);
            return;
        }
        strlcpy(_syncedIP, ip, sizeof(_syncedIP));
        _attempted = false;
        _cfg.lastDnsUpdate = millis();
        _wifi.notifySuccess();
        _log.log(LOGP("DNS updated!"));
    }
};
