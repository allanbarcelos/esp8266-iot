// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

// Install counter for the web installer (install/index.html): the page
// posts /install when a flash finishes; the README's badge reads /badge
// (a shields.io endpoint). Deployed to workers.dev by
// .github/workflows/counter.yml, with one KV namespace (COUNT).
//
// One count per address per day. The address is never stored: only a
// SHA-256 of it with the day and a random salt kept in KV, which expires
// after a day. Only the installer's page may count (Origin); anyone can
// still send that header by hand, so the number is a fair estimate, not
// an exact one.

export const ORIGIN = 'https://allanbarcelos.github.io';
const DAY_S = 86400;

export default {
    async fetch(req, env) {
        const path = new URL(req.url).pathname;
        if (req.method === 'GET' && path === '/badge') return badge(env);
        if (req.method === 'POST' && path === '/install') return install(req, env);
        return new Response('', { status: 404 });
    },
};

async function total(env) {
    return Number(await env.COUNT.get('total')) || 0;
}

// shields.io endpoint JSON; cached five minutes
async function badge(env) {
    return Response.json(
        { schemaVersion: 1, label: 'installs', message: String(await total(env)), color: 'blue' },
        { headers: { 'Cache-Control': 'max-age=300' } });
}

async function install(req, env) {
    if (req.headers.get('Origin') !== ORIGIN) return new Response('', { status: 403 });
    const ip = req.headers.get('CF-Connecting-IP') || '';
    const day = Math.floor(Date.now() / (DAY_S * 1000));
    const seen = 'seen:' + await sha256(ip + '|' + day + '|' + await salt(env));
    if (!await env.COUNT.get(seen)) {
        await env.COUNT.put(seen, '1', { expirationTtl: DAY_S });
        await env.COUNT.put('total', String(await total(env) + 1));
    }
    return new Response(null, { status: 204, headers: { 'Access-Control-Allow-Origin': ORIGIN } });
}

// Made once, on the first count
async function salt(env) {
    let s = await env.COUNT.get('salt');
    if (!s) {
        s = hex(crypto.getRandomValues(new Uint8Array(16)));
        await env.COUNT.put('salt', s);
    }
    return s;
}

async function sha256(text) {
    return hex(new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text))));
}

function hex(bytes) {
    return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
}
