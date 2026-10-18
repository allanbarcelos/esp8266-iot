// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

// Tests of counter/worker.mjs on Node (node --test), with KV imitated
import { test } from 'node:test';
import assert from 'node:assert/strict';
import worker, { ORIGIN } from './worker.mjs';

// KV: get gives null for a missing key; put keeps the options it got
function kv(start = {}) {
    const data = new Map(Object.entries(start));
    const ttl = new Map();
    return {
        data, ttl,
        async get(k) { return data.has(k) ? data.get(k) : null; },
        async put(k, v, opt) { data.set(k, v); if (opt) ttl.set(k, opt.expirationTtl); },
    };
}

const BASE = 'https://esp8266-installs.example.workers.dev';

function post(ip, origin = ORIGIN) {
    const headers = { Origin: origin };
    if (ip) headers['CF-Connecting-IP'] = ip;
    return new Request(BASE + '/install', { method: 'POST', headers });
}

async function count(env) {
    const r = await worker.fetch(new Request(BASE + '/badge'), env);
    return (await r.json()).message;
}

test('the badge is a shields.io endpoint with the total, cached', async () => {
    const env = { COUNT: kv() };
    const r = await worker.fetch(new Request(BASE + '/badge'), env);
    assert.equal(r.status, 200);
    assert.equal(r.headers.get('Cache-Control'), 'max-age=300');
    assert.deepEqual(await r.json(), { schemaVersion: 1, label: 'installs', message: '0', color: 'blue' });
    assert.equal(await count({ COUNT: kv({ total: '41' }) }), '41');
});

test('one count per address per day, the address kept only as a hash for a day', async () => {
    const env = { COUNT: kv({ total: '10' }) };
    const r = await worker.fetch(post('203.0.113.7'), env);
    assert.equal(r.status, 204);
    assert.equal(r.headers.get('Access-Control-Allow-Origin'), ORIGIN);
    assert.equal(await count(env), '11');
    await worker.fetch(post('203.0.113.7'), env);
    assert.equal(await count(env), '11');
    await worker.fetch(post('198.51.100.2'), env);
    assert.equal(await count(env), '12');
    await worker.fetch(post(null), env);   // no address header: one more key, counted once
    await worker.fetch(post(null), env);
    assert.equal(await count(env), '13');

    const seen = [...env.COUNT.data.keys()].filter((k) => k.startsWith('seen:'));
    assert.equal(seen.length, 3);
    for (const k of seen) {
        assert.match(k, /^seen:[0-9a-f]{64}$/);
        assert.equal(env.COUNT.ttl.get(k), 86400);
    }
    assert.ok(![...env.COUNT.data.keys()].some((k) => k.includes('203.0.113.7')));
    // The salt is made once and kept
    assert.match(env.COUNT.data.get('salt'), /^[0-9a-f]{32}$/);
});

test('the next day the same address counts again', async () => {
    const env = { COUNT: kv() };
    const now = Date.now;
    try {
        Date.now = () => 1792022400000;
        await worker.fetch(post('203.0.113.7'), env);
        Date.now = () => 1792022400000 + 86400000;
        await worker.fetch(post('203.0.113.7'), env);
    } finally {
        Date.now = now;
    }
    assert.equal(await count(env), '2');
});

test('only the installer page counts; anything else is not found', async () => {
    const env = { COUNT: kv() };
    assert.equal((await worker.fetch(post('203.0.113.7', 'https://example.com'), env)).status, 403);
    assert.equal((await worker.fetch(new Request(BASE + '/install', { method: 'POST' }), env)).status, 403);
    assert.equal(await count(env), '0');
    assert.equal((await worker.fetch(new Request(BASE + '/install'), env)).status, 404);
    assert.equal((await worker.fetch(new Request(BASE + '/badge', { method: 'POST' }), env)).status, 404);
    assert.equal((await worker.fetch(new Request(BASE + '/'), env)).status, 404);
});
