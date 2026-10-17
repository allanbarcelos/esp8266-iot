# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

"""The panel's text matrix, compressed by context mixing (web/i18n.js decodes it).

The languages share most of their words, and pt and es often whole
sentences, so each cell is predicted from the same row's cell in the
language before it (pt from en, es from pt, fr from es), which the
decoder already has: it decodes column by column. A "parallel" pointer
follows that text and resyncs on the last three bytes; next to it, hashed
contexts of order 1 to 6, the current word, and a match model over all the
text seen. A mixer, chosen by the bit position and by how well the
parallel text has been doing, weighs them; an arithmetic coder writes the
result. Repeated text costs close to nothing, a cognate pays only for what
differs.

Integers only, so web/i18n.js gets the very same numbers in any browser
(Math.exp and Math.log may round differently from one engine to another).
Values stay below 2**53: plain JavaScript numbers hold them exactly, and
divisions go through floor, as Python's // does.

    python tools/i18n_cm.py      round trip of the current table, with sizes
"""

ORDERS = (1, 2, 3, 4, 6)
NIN = len(ORDERS) + 2 + 1 + 2 + 1 + 1   # contexts, order 0, word, parallel (3), match, bias
TBITS = 22                               # one shared hash table of 2**22 counters
MASK = (1 << TBITS) - 1
LR = 10                                  # mixer learning rate (shift)


def _squash_table():
    # 4096 * 1/(1+e^-x) for x = d/256, d in -2047..2047, by the same
    # interpolation lpaq uses: 33 fixed points, linear in between
    pts = [1, 2, 3, 6, 10, 16, 27, 45, 73, 120, 194, 310, 488, 747, 1101, 1546,
           2047, 2549, 2994, 3348, 3607, 3785, 3901, 3975, 4024, 4050, 4068, 4079,
           4085, 4089, 4092, 4093, 4094]
    out = []
    for d in range(-2047, 2048):
        w = d & 127
        i = (d >> 7) + 16
        out.append((pts[i] * (128 - w) + pts[i + 1] * w + 64) >> 7)
    return out


SQ = _squash_table()


def squash(d):
    if d > 2047:
        d = 2047
    if d < -2047:
        d = -2047
    return SQ[d + 2047]


def _stretch_table():
    # inverse of squash: the d whose squash is p (the first one, rising)
    st = [0] * 4096
    pi = 0
    for d in range(-2047, 2048):
        v = squash(d)
        for p in range(pi, v + 1):
            st[p] = d
        pi = v + 1
    for p in range(pi, 4096):
        st[p] = 2047
    return st


ST = _stretch_table()
# 65536 / (n + 1.5), the counters' adaptation: fast at first, then steadier
RECIP = [65536 * 2 // (2 * n + 3) for n in range(32)]
NLIM = 30


def _h(a, b):
    return (a * 0x2F0B3 + b + 1) % 4294967296


class Model:
    def __init__(self):
        self.t = [32768] * (1 << TBITS)
        self.n = bytearray(1 << TBITS)
        self.pt = {}                 # the parallel model's own counters, (run, bit)
        self.w = {}                  # mixer weights, 16.16, per selector
        self.hist = bytearray()
        self.index = {}
        self.mptr = 0
        self.mlen = 0
        self.wordh = 0
        self.ref = b''
        self.q = 0
        self.run = 0

    def cell(self, ref):
        """A new cell, predicted from ref (bytes; b'' for none)."""
        self.ref = ref
        self.q = 0
        self.run = 0

    def _ctxs(self):
        hist = self.hist
        out = []
        for o in ORDERS:
            h = o * 0x9E3779B1 % 4294967296
            for b in hist[len(hist) - o:] if len(hist) >= o else hist:
                h = _h(h, b)
            out.append(h)
        out.append(17)
        out.append((self.wordh ^ 0x55555) + 0x77)
        pexp = self.ref[self.q] if self.q < len(self.ref) else -1
        out.append(_h(_h(0xABC, pexp + 1), min(self.run, 15)))
        return out, pexp

    def code_byte(self, byte, coder):
        """Encodes byte (or decodes one, byte None) through coder.bit(p, bit)."""
        ctxs, pexp = self._ctxs()
        mexp = self.hist[self.mptr] if self.mlen > 0 and self.mptr < len(self.hist) else -1
        c0 = 1
        # Counters by nibble: a context's 15 for the four bits of a nibble sit
        # together (one cache line in the browser), the second nibble's
        # block chosen by the first
        base = [(ch * 0x10001 + m * 0x9E3779) & MASK & ~15 for m, ch in enumerate(ctxs)]
        for i in range(7, -1, -1):
            if i == 3:
                base = [(_h(ch, c0) * 0x10001 + m * 0x9E3779) & MASK & ~15 for m, ch in enumerate(ctxs)]
            local = c0 if i >= 4 else (c0 & ((1 << (3 - i)) - 1)) | (1 << (3 - i))
            st = []
            idx = []
            for m in range(len(ctxs)):
                k = base[m] + local
                idx.append(k)
                st.append(ST[self.t[k] >> 4])
            pk = None
            if pexp >= 0 and ((pexp + 256) >> (i + 1)) == c0:
                eb = (pexp >> i) & 1
                pk = (min(self.run, 31), eb)
                st.append(ST[self.pt.get(pk, 3277 * 16 if eb else 819 * 16) >> 4])
                st.append((1 if eb else -1) * min(self.run + 1, 16) * 64)
            else:
                st.append(0)
                st.append(0)
            mk = None
            if mexp >= 0 and ((mexp + 256) >> (i + 1)) == c0:
                eb = (mexp >> i) & 1
                mk = (100 + min(self.mlen, 31), eb)
                st.append(ST[self.pt.get(mk, 3277 * 16 if eb else 819 * 16) >> 4])
            else:
                st.append(0)
            st.append(256)
            sel = c0 * 8 + (min(self.run, 3) if pexp >= 0 else 4)
            ws = self.w.get(sel)
            if ws is None:
                ws = self.w[sel] = [19661] * NIN    # 0.3
            dot = 0
            for a, b in zip(ws, st):
                dot += a * b
            p = squash(dot // 65536)
            if p < 1:
                p = 1
            if p > 4095:
                p = 4095
            bit = coder.bit(p, None if byte is None else (byte >> i) & 1)
            err = (bit << 12) - p
            for j in range(NIN):
                ws[j] += (st[j] * err) // (1 << LR)
            for k in idx:
                n = self.n[k]
                self.t[k] += (((bit << 16) - self.t[k]) * RECIP[n]) // 65536
                if n < NLIM:
                    self.n[k] = n + 1
            for kk in (pk, mk):
                if kk is not None:
                    key = kk + ('n',)
                    n = self.pt.get(key, 0)
                    v = self.pt.get(kk, 3277 * 16 if kk[1] else 819 * 16)
                    self.pt[kk] = v + (((bit << 16) - v) * RECIP[n]) // 65536
                    if n < NLIM:
                        self.pt[key] = n + 1
            c0 = c0 * 2 + bit
        byte = c0 & 255
        self._after(byte)
        return byte

    def _after(self, byte):
        ref, q = self.ref, self.q
        if q < len(ref) and ref[q] == byte:
            self.q = q + 1
            self.run += 1
        else:
            self.run = 0
            if len(self.hist) >= 2:
                tail = bytes(self.hist[-2:]) + bytes([byte])
                k = ref.find(tail, max(0, q - 8))
                if k < 0:
                    k = ref.find(tail)
                self.q = k + 3 if k >= 0 else q + 1
            else:
                self.q = q + 1
        if self.mlen > 0 and self.mptr < len(self.hist) and self.hist[self.mptr] == byte:
            self.mlen += 1
            self.mptr += 1
        else:
            self.mlen = 0
        self.hist.append(byte)
        if len(self.hist) >= 5:
            h = bytes(self.hist[-5:])
            if self.mlen == 0 and h in self.index:
                self.mptr = self.index[h]
                self.mlen = 1
            self.index[h] = len(self.hist)
        if 65 <= byte <= 90 or 97 <= byte <= 122 or byte >= 128:
            self.wordh = (self.wordh * 263 + (byte | 32)) % 16777216
        else:
            self.wordh = 0


class Encoder:
    def __init__(self):
        self.x1, self.x2 = 0, 0xFFFFFFFF
        self.out = bytearray()

    def bit(self, p, bit):
        r = self.x2 - self.x1
        xmid = self.x1 + (r >> 12) * p + (((r & 4095) * p) >> 12)
        if bit:
            self.x2 = xmid
        else:
            self.x1 = xmid + 1
        while (self.x1 ^ self.x2) & 0xFF000000 == 0:
            self.out.append(self.x2 >> 24)
            self.x1 = (self.x1 << 8) & 0xFFFFFFFF
            self.x2 = ((self.x2 << 8) & 0xFFFFFFFF) | 255
        return bit

    def finish(self):
        # all of x1: the decoder, reading zeros past the end, stays inside the range
        self.out += self.x1.to_bytes(4, 'big')
        return bytes(self.out)


class Decoder:
    def __init__(self, data):
        self.data = data
        self.at = 0
        self.x1, self.x2 = 0, 0xFFFFFFFF
        self.x = 0
        for _ in range(4):
            self.x = (self.x << 8) | self._next()

    def _next(self):
        b = self.data[self.at] if self.at < len(self.data) else 0
        self.at += 1
        return b

    def bit(self, p, _):
        r = self.x2 - self.x1
        xmid = self.x1 + (r >> 12) * p + (((r & 4095) * p) >> 12)
        bit = 1 if self.x <= xmid else 0
        if bit:
            self.x2 = xmid
        else:
            self.x1 = xmid + 1
        while (self.x1 ^ self.x2) & 0xFF000000 == 0:
            self.x1 = (self.x1 << 8) & 0xFFFFFFFF
            self.x2 = ((self.x2 << 8) & 0xFFFFFFFF) | 255
            self.x = ((self.x << 8) & 0xFFFFFFFF) | self._next()
        return bit


SEP_CELL, SEP_COL = 0x7E, 0x7C   # ~ and |, as in embed_web.matrix


def _resolve(cells, j, r):
    """The text a decoded cell stands for: "" is English, "^" the language before."""
    s = cells[j][r]
    if j and s == b'':
        return cells[0][r]
    if j > 1 and s == b'^':
        return _resolve(cells, j - 1, r)
    return s


def encode(matrix):
    """matrix: embed_web.matrix's text (columns by |, cells by ~) → bytes."""
    cols = [[c.encode() for c in col.split('~')] for col in matrix.split('|')]
    m, e = Model(), Encoder()
    for j, col in enumerate(cols):
        for r, cell in enumerate(col):
            m.cell(_resolve(cols, j - 1, r) if j else b'')
            for b in cell:
                m.code_byte(b, e)
            m.code_byte(SEP_CELL if r < len(col) - 1 else SEP_COL, e)
    return e.finish()


def decode(data, rows, langs):
    """The matrix text back from encode's bytes (rows and langs: its shape)."""
    m, d = Model(), Decoder(data)
    cols = []
    for j in range(langs):
        col = []
        cols.append(col)
        for r in range(rows):
            m.cell(_resolve(cols, j - 1, r) if j else b'')
            cell = bytearray()
            while True:
                b = m.code_byte(None, d)
                if b in (SEP_CELL, SEP_COL):
                    break
                cell += bytes([b])
                if len(cell) > 4096:
                    raise ValueError('not a matrix')
            col.append(bytes(cell))
    return '|'.join('~'.join(c.decode() for c in col) for col in cols)


if __name__ == '__main__':
    import os
    import sys
    import time
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import embed_web as E
    rows = E.similar_first(E.load_table())
    mat = E.matrix(rows)
    t = time.time()
    data = encode(mat)
    t1 = time.time()
    back = decode(data, len(rows), len(E.LANGS))
    t2 = time.time()
    print(f'{len(mat.encode())} bytes -> {len(data)} ({t1 - t:.1f} s, decode {t2 - t1:.1f} s), '
          f'round trip {"ok" if back == mat else "FAILED"}')
