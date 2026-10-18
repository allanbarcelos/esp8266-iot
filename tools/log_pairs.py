# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: https://github.com/allanbarcelos/esp8266-iot

"""Made LogPairs.h: the byte pairs that pack the log lines kept in LittleFS.

The table is frozen: the log files outlive an update, and a file written by
one firmware must read the same on the next. New log texts still pack, only
less. Picked once from the log formats of the firmware (their %-fields
filled with typical values), the most frequent pairs first, like
tools/html_pairs.py.

Usage: python tools/log_pairs.py --force   rewrites LogPairs.h; old log files
                                           then read as garbage (do not)
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import html_pairs as hp  # noqa: E402

ROOT = hp.ROOT
FILL = [b"example.com", b"42", b"1234", b"86400", b"v165", b"192.168.1.20", b"-61"]


def lines():
    out = []
    for name in sorted(f for f in os.listdir(ROOT) if f.endswith(".h")):
        text = hp.read(name)
        for m in re.finditer(r"(?:_log\.log|logger\.log|_log->log)\(\s*(?:PSTR\()?((?:\s*" + hp.LIT + r")+)", text):
            fmt = b"".join(s[1:-1].encode().decode("unicode_escape").encode("latin-1")
                           for s in re.findall(hp.LIT, m.group(1)))
            k = [len(out)]

            def fill(_):
                k[0] += 1
                return FILL[k[0] % len(FILL)]
            out.append(re.sub(rb"%[-0-9.]*l?[sduxi]", fill, fmt))
    return sorted(set(out))


def main():
    path = os.path.join(ROOT, "LogPairs.h")
    if os.path.exists(path) and "--force" not in sys.argv[1:]:
        sys.exit("LogPairs.h is frozen (see the docstring)")
    found = lines()
    table, packed = hp.pairs(found)
    raw = sum(len(t) + 1 for t in found)
    rows = "\n".join(f"    {{0x{a:02x}, 0x{b:02x}}},  // {hp.show(a, table) + hp.show(b, table)!r}"
                     for a, b in table)
    text = (
        "// SPDX-License-Identifier: MIT\n"
        "// Copyright (c) 2026 Allan Barcelos. ESP8266 IoT: "
        "https://github.com/allanbarcelos/esp8266-iot\n\n"
        "#pragma once\n"
        "// Made once by tools/log_pairs.py from the log texts. Frozen: log files\n"
        "// written by an older firmware must read the same. Do not regenerate.\n"
        "#include <Arduino.h>\n\n"
        f"// Byte 0x80 + i of a packed log line stands for pair i ({raw} bytes of\n"
        f"// texts became {packed})\n"
        f"static constexpr uint8_t LOG_PAIR_COUNT = {len(table)};\n"
        "static constexpr uint8_t LOG_PAIRS[][2] PROGMEM = {\n"
        f"{rows}\n"
        "};\n"
    )
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"LogPairs.h: {len(found)} texts, {raw} -> {packed} bytes, {len(table)} pairs")


if __name__ == "__main__":
    main()
