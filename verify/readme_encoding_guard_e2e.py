#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""readme_encoding_guard_e2e.py -- prove the guard actually inverts the damage.

WHY THIS EXISTS

The guard's own gate can only prove what the damage determines.  A marker's two
lost bytes are, by definition, not determined -- so a wrong guess about them
still passes the gate.  This harness closes that gap from the outside: it takes
the clean file, damages it with the same round trip, runs the tool's own repair
on a COPY, and then requires the copy to come back byte-identical to the
original.  Nothing about the repair is allowed to be assumed correct.

It found three real defects that the internal gate had been reporting as
"only one violation", all of them the same shape -- a byte that the
comparison could not see:

  * a marker that swallowed the CR of the last row on a line, which the gate
    could not count once both sides were stripped of their terminator
  * a marker completing a TWO-byte character (a leading 0xC2 or 0xCE), which
    the symbol table only covered for three-byte shapes
  * a donor row spliced in without its line terminator, because git stores the
    blob LF-normalised and the damaged row had no CR left to copy from

It also measured the residue the gate cannot reach: 7 of 318 rows, 10
character positions, where the swallowed character was not a space and a
same-prefix symbol was not the most common one.  Those are reported, not
silently accepted.

  python verify/readme_encoding_guard_e2e.py
  exit 0 = the tool recovered the file exactly, or recovered everything the
  gate can prove and listed the rest.
"""
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "verify"))
import readme_encoding_guard as G  # noqa: E402

TARGET_REL = "mbt/README.md"
SCRATCH = os.path.join(REPO, "_build", "e2e_readme_copy.md")
PY = sys.executable


def run(mode, *extra):
    return subprocess.run(
        [PY, "-X", "utf8", os.path.join(REPO, "verify", "readme_encoding_guard.py"),
         mode, "--target", TARGET_REL, *extra],
        capture_output=True, cwd=REPO,
    )


def main():
    src = os.path.join(REPO, TARGET_REL)
    clean = open(src, "rb").read()

    # ---- the file must be clean for this test to mean anything -------------
    p = run("check")
    if p.returncode != 0:
        sys.stdout.write(p.stdout.decode("utf-8", "replace"))
        print("SKIP: %s is not clean, so there is no reference to recover."
              % TARGET_REL)
        return 0
    print("reference     : %s, %d bytes, clean" % (TARGET_REL, len(clean)))

    # ---- damage a copy, exactly as the round trip did ---------------------
    text = clean.decode("utf-8-sig")
    dmg_text = "\n".join(G.corrupt(l.encode("utf-8")) for l in text.split("\n"))
    dmg = (b"\xef\xbb\xbf" if clean[:3] == b"\xef\xbb\xbf" else b"") + dmg_text.encode("utf-8")
    os.makedirs(os.path.dirname(SCRATCH), exist_ok=True)
    shutil.copyfile(src, SCRATCH)
    with open(SCRATCH, "wb") as fh:
        fh.write(dmg)
    rel_scratch = os.path.relpath(SCRATCH, REPO).replace("\\", "/")
    print("damaged copy  : %d bytes, CJK %d, markers %d"
          % (len(dmg), sum(1 for c in dmg_text if G.is_cjk(c)),
             sum(1 for l in dmg_text.split("\n") for i in G.slots(l) if l[i] == "?")))

    # ---- check mode must notice, on the copy ------------------------------
    p = subprocess.run(
        [PY, "-X", "utf8", os.path.join(REPO, "verify", "readme_encoding_guard.py"),
         "check", "--target", rel_scratch],
        capture_output=True, cwd=REPO)
    if p.returncode != 1:
        print("FAIL: check did not report the damage (exit %d)" % p.returncode)
        return 1
    print("check on copy : exit 1, DAMAGED, as required")

    # ---- repair the copy, with the real file as the donor -----------------
    p = subprocess.run(
        [PY, "-X", "utf8", os.path.join(REPO, "verify", "readme_encoding_guard.py"),
         "repair", "--target", rel_scratch, "--donor-path", TARGET_REL],
        capture_output=True, cwd=REPO)
    out = p.stdout.decode("utf-8", "replace")
    if p.returncode != 0:
        sys.stdout.write(out)
        print("FAIL: repair exit %d" % p.returncode)
        sys.stdout.write(p.stderr.decode("utf-8", "replace")[-1200:])
        return 1
    for line in out.splitlines():
        if line.startswith(("stage", "rows changed", "ACCEPTANCE", "  0 violations",
                           "JUDGEMENT ZONE")):
            print("  " + line[:110])

    got = open(SCRATCH, "rb").read()
    if got == clean:
        print("\nBYTE-IDENTICAL to the reference: yes")
        print("E2E PASS -- every character, including the ones the gate cannot "
              "prove, was recovered")
        return 0

    # ---- otherwise: quantify exactly what the gate cannot reach ------------
    o = clean.decode("utf-8-sig").split("\n")
    g = got.decode("utf-8-sig").split("\n")
    diff = [n for n, (a, b) in enumerate(zip(o, g), 1) if a != b]

    # Recompute the judgement zone: the rows the donor could not supply, which
    # are the only rows where an inferred symbol or an inferred swallowed
    # character is possible at all.  A difference anywhere else is a defect.
    dmg_lines = dmg_text.split("\n")
    crlf = sum(1 for l in dmg_lines if l.endswith("\r")) * 2 > len(dmg_lines)
    _, sst = G.splice_donor(dmg_lines, G.git_blob("HEAD:%s" % TARGET_REL)
                            .decode("utf-8-sig").split("\n"), crlf=crlf)
    zone = {n for n, l in enumerate(dmg_lines, 1)
            if G.line_damaged(l) and (n - 1) not in sst["at"]}
    outside = [n for n in diff if n not in zone]

    print("\nBYTE-IDENTICAL to the reference: no")
    print("rows differing   : %d of %d damaged -- all inside the judgement zone: %s"
          % (len(diff), sum(1 for l in dmg_lines if G.line_damaged(l)), not outside))
    print("  rows: %s" % ", ".join(str(n) for n in diff[:40]))
    if outside:
        print("  OUTSIDE the judgement zone (defects): %s"
              % ", ".join(str(n) for n in outside))
    print("\nThese are the positions the gate cannot decide by construction: a")
    print("marker's two lost bytes, and which of two symbols sharing a 2-byte")
    print("prefix was meant.  Re-run with --symbol PREFIX=CHAR to pin one, and")
    print("read the rows above to fix the swallowed character by hand.")
    for n in diff[:4]:
        for i, (a, b) in enumerate(zip(o[n - 1], g[n - 1])):
            if a != b:
                print("  L%d col %d: reference %r  repaired %r" % (n, i, a, b))
                print("       reference ...%s" % o[n - 1][max(0, i - 30):i + 20])
                print("       repaired ...%s" % g[n - 1][max(0, i - 30):i + 20])
                break
    if outside:
        print("\nE2E FAIL -- a row the donor could have supplied came back wrong")
        return 1
    print("\nE2E PASS -- every determinable character recovered; the %d row(s) "
          "above are\n         the documented residue, listed rather than "
          "assumed correct" % len(diff))
    return 0


if __name__ == "__main__":
    sys.exit(main())
