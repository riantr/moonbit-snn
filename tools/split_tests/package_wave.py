#!/usr/bin/env python3 -X utf8
"""Materialise the wave-1 test sub-package.

Reads `moon.pkg` template, writes each selected test file into
`mbt/tests/wave1/` with `@snn_mbt.` qualification applied. The originals are
left in place so the root package keeps working until wave 1 is proven green.
"""
import os
import sys

sys.path.insert(0, r"D:\src\MiniMax\Projects\MoonBit\moonbit-snn\_build")
from split_qualify import load_public, qualify  # noqa: E402

MBT = r"D:\src\MiniMax\Projects\MoonBit\moonbit-snn\mbt"
DST = os.path.join(MBT, "tests", "wave1")

MOON_PKG = """// moon.pkg for the wave-1 test sub-package.
//
// Every test file in here was moved out of the root package so that the root
// package's test target no longer has to codegen as a single C translation
// unit. Access to the package under test is via the blackbox import below;
// `moon.pkg` (not `moon.pkg.json`) is the per-directory manifest name.

import {
  "riantr/snn_mbt",
}

options(
  "is-test": true,
)
"""


def main():
    with open(r"D:\src\MiniMax\Projects\MoonBit\moonbit-snn\_build\pick.txt",
              encoding="utf-8-sig") as fh:
        picks = [l.strip() for l in fh if l.strip() and not l.startswith("MISSING")]

    os.makedirs(DST, exist_ok=True)
    with open(os.path.join(DST, "moon.pkg"), "w", encoding="utf-8", newline="") as fh:
        fh.write(MOON_PKG)

    types, free_fns, values = load_public()
    grand = 0
    for name in picks:
        src_path = os.path.join(MBT, name)
        # newline='' so CRLF/LF in the original survives byte-for-byte
        with open(src_path, encoding="utf-8", newline="") as fh:
            src = fh.read()
        new, st = qualify(src, types, free_fns, values)
        n = st["type_method"] + st["bare"]
        grand += n
        with open(os.path.join(DST, name), "w", encoding="utf-8", newline="") as fh:
            fh.write(new)
        print(f"  {name:<44} {n:>4} prefixes")
    print(f"\n{len(picks)} files -> {DST}")
    print(f"total prefixes = {grand}")


if __name__ == "__main__":
    main()
