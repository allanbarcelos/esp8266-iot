// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

// The panel's text matrix, decoded from tools/i18n_cm.py's context mixing
// (that file explains the model). The same integers in the same order, so
// every browser gets the very bytes the tool encoded: no Math.exp or
// Math.log, values below 2^53 and divisions through Math.floor (Python's //).
// b: the base64 bytes; rows: cells per column; four columns (en|pt|es|fr).
// Returns the matrix text: columns joined by |, cells by ~.
function IZ(b, rows) {
    var T = 1 << 22, t = new Uint16Array(T).fill(32768), tn = new Uint8Array(T),
        pt = new Uint16Array(128), pn = new Uint8Array(128), st = new Int32Array(12), idx = new Int32Array(8), c = new Uint32Array(8), bs = new Int32Array(8), L = [1, 2, 3, 4, 6], W = new Float64Array(2048 * 12).fill(19661),
        SQ = new Int16Array(4095), ST = new Int16Array(4096), RC = [], P = [1, 2, 3, 6, 10, 16, 27, 45, 73, 120, 194, 310, 488, 747, 1101, 1546,
            2047, 2549, 2994, 3348, 3607, 3785, 3901, 3975, 4024, 4050, 4068, 4079, 4085, 4089, 4092, 4093, 4094],
        O = [2654435761, 1013904226, 3668339987, 2027808452, 3041712678], hist = [], index = new Map(), mptr = 0, mlen = 0, wordh = 0,
        ref = [], q = 0, run = 0, x1 = 0, x2 = 4294967295, x = 0, at = 0, cols = [], j, r, i, d, p, v, out = [];
    b = atob(b);
    for (i = 0; i < 128; i++) pt[i] = i & 1 ? 52432 : 13104;
    for (d = -2047; d < 2048; d++) SQ[d + 2047] = ((P[(d >> 7) + 16] * (128 - (d & 127)) + P[(d >> 7) + 17] * (d & 127) + 64) >> 7);
    for (p = 0, d = -2047; d < 2048; d++) for (v = SQ[d + 2047]; p <= v; p++) ST[p] = d;
    for (; p < 4096; p++) ST[p] = 2047;
    for (i = 0; i < 32; i++) RC[i] = Math.floor(131072 / (2 * i + 3));
    function next() { return at < b.length ? b.charCodeAt(at++) : (at++, 0); }
    function h(a, c) { return (Math.imul(a, 192691) + c + 1) >>> 0; }
    function sq(d) { return SQ[Math.max(-2047, Math.min(2047, d)) + 2047]; }
    function find(s, from) {
        for (var k = Math.max(0, from); k + 3 <= ref.length; k++)
            if (ref[k] == s[0] && ref[k + 1] == s[1] && ref[k + 2] == s[2]) return k;
        return -1;
    }
    function adapt(a, n, k, bit) {
        a[k] += Math.floor(((bit << 16) - a[k]) * RC[n[k]] / 65536);
        if (n[k] < 30) n[k]++;
    }
    function byte() {
        var o, k, m, s, c0 = 1, n = hist.length, pexp = q < ref.length ? ref[q] : -1,
            mexp = mlen > 0 && mptr < n ? hist[mptr] : -1, pk, mk, eb, ws, dot, bit, xmid, rr, e;
        for (o = 0; o < 5; o++) {
            for (k = O[o], m = Math.max(0, n - L[o]); m < n; m++) k = h(k, hist[m]);
            c[o] = k;
        }
        c[5] = 17;
        c[6] = (wordh ^ 349525) + 119;
        c[7] = h(h(2748, pexp + 1), Math.min(run, 15));
        st[11] = 256;
        // Counters by nibble (tools/i18n_cm.py): the second one's block by the first
        for (m = 0; m < 8; m++) bs[m] = (Math.imul(c[m], 65537) + m * 10368889) & (T - 16);
        for (i = 7; i >= 0; i--) {
            if (i == 3) for (m = 0; m < 8; m++) bs[m] = (Math.imul(h(c[m], c0), 65537) + m * 10368889) & (T - 16);
            o = i > 3 ? c0 : c0 & (1 << 3 - i) - 1 | 1 << 3 - i;
            for (m = 0; m < 8; m++) {
                idx[m] = k = bs[m] + o;
                st[m] = ST[t[k] >> 4];
            }
            pk = mk = -1;
            st[8] = st[9] = st[10] = 0;
            if (pexp >= 0 && (pexp + 256) >> (i + 1) == c0) {
                eb = pexp >> i & 1;
                pk = Math.min(run, 31) * 2 + eb;
                st[8] = ST[pt[pk] >> 4];
                st[9] = (eb ? 64 : -64) * Math.min(run + 1, 16);
            }
            if (mexp >= 0 && (mexp + 256) >> (i + 1) == c0) {
                eb = mexp >> i & 1;
                mk = 64 + Math.min(mlen, 31) * 2 + eb;
                st[10] = ST[pt[mk] >> 4];
            }
            ws = (c0 * 8 + (pexp >= 0 ? Math.min(run, 3) : 4)) * 12;
            for (dot = 0, m = 0; m < 12; m++) dot += W[ws + m] * st[m];
            p = Math.max(1, Math.min(4095, sq(Math.floor(dot / 65536))));
            rr = x2 - x1;
            xmid = x1 + Math.floor(rr / 4096) * p + ((rr % 4096) * p >> 12);
            bit = x <= xmid ? 1 : 0;
            if (bit) x2 = xmid;
            else x1 = xmid + 1;
            while (Math.floor(x1 / 16777216) == Math.floor(x2 / 16777216)) {
                x1 = x1 % 16777216 * 256;
                x2 = x2 % 16777216 * 256 + 255;
                x = x % 16777216 * 256 + next();
            }
            e = (bit << 12) - p;
            for (m = 0; m < 12; m++) W[ws + m] += Math.floor(st[m] * e / 1024);
            for (m = 0; m < 8; m++) adapt(t, tn, idx[m], bit);
            if (pk >= 0) adapt(pt, pn, pk, bit);
            if (mk >= 0) adapt(pt, pn, mk, bit);
            c0 = c0 * 2 + bit;
        }
        s = c0 & 255;
        // The parallel text: on, or found again by the last three bytes
        if (q < ref.length && ref[q] == s) { q++; run++; } else {
            run = 0;
            if (n >= 2) {
                k = [hist[n - 2], hist[n - 1], s];
                m = find(k, q - 8);
                if (m < 0) m = find(k, 0);
                q = m >= 0 ? m + 3 : q + 1;
            } else q++;
        }
        if (mlen > 0 && mptr < n && hist[mptr] == s) { mlen++; mptr++; } else mlen = 0;
        hist.push(s);
        if (++n >= 5) {
            k = (((hist[n - 5] * 256 + hist[n - 4]) * 256 + hist[n - 3]) * 256 + hist[n - 2]) * 256 + hist[n - 1];
            if (mlen == 0 && index.has(k)) { mptr = index.get(k); mlen = 1; }
            index.set(k, n);
        }
        wordh = s > 64 && s < 91 || s > 96 && s < 123 || s > 127 ? (wordh * 263 + (s | 32)) % 16777216 : 0;
        return s;
    }
    // A cell's text: "" is the English, "^" the language before
    function res(j, r) {
        var s = cols[j][r];
        return j && !s.length ? cols[0][r] : j > 1 && s.length == 1 && s[0] == 94 ? res(j - 1, r) : s;
    }
    for (i = 0; i < 4; i++) x = x * 256 + next();
    for (j = 0; j < 4; j++) {
        cols.push([]);
        for (r = 0; r < rows; r++) {
            ref = j ? res(j - 1, r) : [];
            q = run = 0;
            // A cell ends on a separator; data that is no matrix runs into the limit
            for (v = []; (d = byte()) != 126 && d != 124;) if (v.push(d) > 4096) throw Error('not a matrix');
            cols[j].push(v);
            out.push.apply(out, v);
            out.push(d);
        }
    }
    return new TextDecoder().decode(new Uint8Array(out.slice(0, -1)));
}
