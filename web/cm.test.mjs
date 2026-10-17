// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

// Tests of web/cm.js on Node (node --test): the matrix the firmware carries
// (WebAssets.h, compressed by tools/i18n_cm.py) decodes to exactly the text
// of web/i18n.tsv, in every language. The decoder must give the very bytes
// the tool encoded: one bit off and every text after it comes out wrong.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { gunzipSync } from 'node:zlib';
import vm from 'node:vm';

const here = (f) => fileURLToPath(new URL(f, import.meta.url));
// CM_JS: the same tests on the minified decoder (tools/embed_web.py --min-cm)
const FILE = process.env.CM_JS || here('./cm.js');

function decoder() {
  const ctx = vm.createContext({ atob, TextDecoder });
  vm.runInContext(readFileSync(FILE, 'utf8'), ctx, { filename: FILE });
  return ctx.IZ;
}

// i18n.js as the firmware serves it: the gzip in WebAssets.h
function shipped() {
  const h = readFileSync(here('../WebAssets.h'), 'utf8');
  const a = h.match(/WEB_I18N_GZ\[\] PROGMEM = \{([^}]*)\}/)[1];
  const js = gunzipSync(Buffer.from(a.match(/0x[0-9a-f]{2}/g).map((x) => parseInt(x, 16)))).toString('utf8');
  const m = js.match(/IZ\("([A-Za-z0-9+/=]+)",(\d+)\)/);
  return { b: m[1], rows: +m[2] };
}

// web/i18n.tsv: English → [en, pt, es, fr], an empty cell is the English
function table() {
  const t = new Map();
  for (const line of readFileSync(here('./i18n.tsv'), 'utf8').split(/\r?\n/)) {
    if (!line || line.startsWith('#')) continue;
    const [, en, ...rest] = line.split('\t');
    t.set(en, [en, ...[0, 1, 2].map((i) => rest[i] || en)]);
  }
  return t;
}

test('the matrix in the firmware decodes to web/i18n.tsv, every language', () => {
  const { b, rows } = shipped();
  const cols = decoder()(b, rows).split('|').map((c) => c.split('~'));
  assert.equal(cols.length, 4);
  const want = table();
  assert.equal(rows, want.size);
  const seen = new Set();
  for (let r = 0; r < rows; r++) {
    const en = cols[0][r];
    // "" is the English, "^" the language before
    const row = [en];
    for (let j = 1; j < 4; j++) row.push(cols[j][r] === '' ? en : cols[j][r] === '^' ? row[j - 1] : cols[j][r]);
    assert.deepEqual(row, want.get(en), en);
    seen.add(en);
  }
  assert.equal(seen.size, want.size);
});

test('bytes that are not a matrix end in an error, not in a loop', () => {
  assert.throws(() => decoder()('AAAA', 1), /not a matrix/);
});
