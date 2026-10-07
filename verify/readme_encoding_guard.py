#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""readme_encoding_guard.py -- detect, and optionally undo, a GBK round trip
over mbt/README.md.

WHAT THIS IS FOR

At some point mbt/README.md was rewritten through the Windows ANSI code page
(Get-Content | Set-Content under a cp936 locale).  UTF-8 bytes were decoded as
GBK, so every box-drawing and symbol character was replaced: 767 CJK mojibake
characters and 396 '?' markers.  The damage was committed and sat unnoticed for
four months.  It was undone in commit ee04b8c; this script is the reusable part,
because the same mistake is one PowerShell pipeline away.

  python verify/readme_encoding_guard.py selftest
  python verify/readme_encoding_guard.py check
  python verify/readme_encoding_guard.py repair --donor HEAD

Exit codes:  0 pass   1 damage present   2 repair gate failed   3 bad usage

THE MODEL, IN ONE RULE

    b < 0x80          -> that ASCII char, consume 1
    b == 0x80         -> U+20AC, consume 1
    b >= 0x81         -> if the next byte is a valid GBK trail: one GBK char,
                         consume 2
                         else: '?', consume 2

That is all of it.  There is no special case for the em dash, the check mark or
the tree drawing; they fall out of the single rule.  Evidence, measured in
_build/roundtrip.txt during the original repair:

    '# snn_mbt - MoonBit port'   (em dash)   ->  U+9225 '?'  'M'   space eaten
    '| Unit system | [check] done'            ->  U+9241 '?'  'd'   space eaten
    '    +-- moon.mod'  (three box chars)    ->  4 CJK + U+20AC, no '?' at all
    '|   +-- main.mbt' (vertical bar first)  ->  U+9239 '?' then only 2 of 3
                                                  spaces survive, bar gone

Note the third line: a character whose third UTF-8 byte is 0x80 leaves a euro
sign, not a marker, and eats nothing.  That is why the marker count and the
character count are different numbers.

ONE INVARIANT THE AUTHOR HAS TO KEEP

`slots()` treats EVERY CJK character as damage, and that is correct here
only because this README legitimately contains none: after the repair its CJK
count is 0, and the corruption is what introduced all 767 of the originals.
So writing a single Chinese phrase into this file makes the gate report
DAMAGED on a line nobody damaged.

That is not a bug to route around -- the message is localised, so quote the
English form (`CreateProcessW: The filename or extension is too long`) and
note the locale instead.  But it is a trap worth stating: if this file ever
gains CJK on purpose, this gate has to learn the difference, not have the
check quietly relaxed.

WHY THIS FILE IS THE ONLY PLACE THE MODEL LIVES

The forward function is used twice: to invert damage, and to prove the
inversion.  An earlier attempt guessed a reverse mapping table from the
character inventory and "repaired" 767 characters down to 219 -- while silently
eating a letter out of `E->I1`.  Guessing a reverse map is the failure mode this
script exists to prevent, so the only artefact is the forward function, and
every repair must reproduce the observed damage byte for byte before it is
allowed to be written.

WHAT IS NOT RECOVERABLE, MEASURED RATHER THAN ASSUMED

verify/readme_encoding_guard_e2e.py damages a copy of the clean file with this
same model, repairs the copy, and requires it back byte for byte.  On
mbt/README.md that recovers 311 of 318 damaged rows exactly.  The other 7 are
the two things the damage does not determine:

  * the single character each '?' swallowed -- almost always a space, but ten
    sites had '2', '1', '(', '-' or ',' instead
  * which of two symbols sharing a 2-byte prefix was meant.  This README uses
    both the set-membership sign and the square root under prefix E288, seven
    times to two, so donor frequency picks the wrong one for 'sqrt(2/pi)'.

The gate cannot see either, by construction: the lost bytes are the hole it is
matching around.  So the repair reports them as a JUDGEMENT ZONE -- the rows
the donor could not supply -- and `--symbol [LINE:]PREFIX=CHAR` lets a human
pin the tie.  The rule is: proven where it can be proven, listed where it
cannot.

A pin closes a row only when that row uses the pinned prefix for one meaning.
Row 77 of this README uses E288 twice for two different symbols, a square root
and a partial derivative, so no per-prefix pin can decide it and the row stays
in the zone.  That is the honest answer, not a gap to be papered over: the
information is not in the damage, and a tool that pretends otherwise is the
thing this file was written to stop.
"""
import argparse
import io
import os
import subprocess
import sys
from collections import Counter, defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REL_DEFAULT = "mbt/README.md"
EURO = "\u20ac"

EXIT_OK, EXIT_DAMAGE, EXIT_GATE, EXIT_USAGE = 0, 1, 2, 3


# --------------------------------------------------------------------------
# the corruption itself
# --------------------------------------------------------------------------
def gbk_char(lead, trail):
    for enc in ("cp936", "gbk", "gb18030"):
        try:
            return bytes([lead, trail]).decode(enc)
        except UnicodeDecodeError:
            continue
    return None


def valid_trail(t):
    return 0x40 <= t <= 0xFE and t != 0x7F


def corrupt(data: bytes) -> str:
    """Push original bytes through the round trip.  The verifier."""
    out = []
    i, n = 0, len(data)
    while i < n:
        b = data[i]
        if b < 0x80:
            out.append(chr(b))
            i += 1
        elif b == 0x80:
            out.append(EURO)
            i += 1
        else:
            if i + 1 < n and valid_trail(data[i + 1]):
                c = gbk_char(b, data[i + 1])
                if c is not None:
                    out.append(c)
                    i += 2
                    continue
            out.append("?")
            i += 2 if (i + 1 < n and not valid_trail(data[i + 1])) else 1
    return "".join(out)


# --------------------------------------------------------------------------
# what counts as damage
# --------------------------------------------------------------------------
def is_cjk(ch):
    """Only CJK can be a pipe product -- the real text is English with symbols,
    never Han characters.  A line containing CJK went through the round trip.
    U+20AC is the other pipe product: byte 0x80."""
    o = ord(ch)
    return (
        0x4E00 <= o <= 0x9FFF
        or 0x3400 <= o <= 0x4DBF
        or 0x3000 <= o <= 0x303F
        or 0xFF00 <= o <= 0xFFEF
        or 0x2E80 <= o <= 0x2EFF
    )


def has_cjk(text):
    return any(is_cjk(c) for c in text)


def slots(line):
    """Indices that are damage.  Compute this ONCE per line: an earlier version
    called it per character and went quadratic on the long rows."""
    bad = {i for i, c in enumerate(line) if is_cjk(c) or c == EURO}
    if not bad:
        return bad
    # A '?' is a marker only next to CJK; the file also contains genuine
    # question marks, as in ascii_plot(width?, height?).
    for i, c in enumerate(line):
        if c == "?":
            if any((i - d) in bad or (i + d) in bad for d in (1, 2)):
                bad.add(i)
    return bad


def is_damage_run(s, i):
    if i >= len(s):
        return False
    ch = s[i]
    if is_cjk(ch) or ch == EURO:
        return True
    if ch == "?":
        for d in (1, 2):
            for t in (i - d, i + d):
                if 0 <= t < len(s) and (is_cjk(s[t]) or s[t] == EURO):
                    return True
    return False


def line_damaged(line):
    return bool(slots(line))


def seqlen(b):
    if b < 0x80:
        return 1
    if 0xC0 <= b <= 0xDF:
        return 2
    if 0xE0 <= b <= 0xEF:
        return 3
    if 0xF0 <= b <= 0xF7:
        return 4
    return 1


# --------------------------------------------------------------------------
# symbol vocabulary, learned from a clean donor
# --------------------------------------------------------------------------
def parse_symbol_pins(specs):
    """Parse --symbol specs into {(line_or_0, width, prefix): char}.

    Accepted forms:
        E288=sqrt          pin every E288 slot in the file
        77:E288=sqrt       pin only row 77

    The line form is the one that matters.  A prefix on its own is too coarse:
    this README uses E288 for the set-membership sign, the square root AND the
    infinity sign, so pinning the prefix to any one of them just moves the
    error.  Naming the row is what actually closes the gap, which is why the
    report prints the row numbers with the residues.
    """
    pins = {}
    for spec in specs or ():
        head, _, rest = spec.partition(":")
        prefix, eq, want = rest.partition("=")
        if not eq:
            raise SystemExit("--symbol %s: expected PREFIX=CHAR" % spec)
        line = int(head) if head.strip().isdigit() else 0
        width = "2" if len(prefix.strip()) == 2 else "3"
        pins[(line, width, prefix.strip().upper())] = want.strip()
    return pins


def build_vocab(donor_text):
    """prefix -> candidate symbols, most-used first.

    A damaged CJK char's GBK encoding IS the original's UTF-8 bytes, so a
    marker carries either the first two bytes of a 3-byte character (an em
    dash, an arrow) or the first byte of a 2-byte one (a plus-minus, a mu) --
    the second case is a single leading byte, and leaving it out of the table
    is what made the inversion give up on 'gelu(+-1) approx ...'.

    Ranking by donor frequency is what stops an em dash being turned into an
    en dash: taking the first candidate by code point picks the wrong one."""
    vocab = {c for c in donor_text if ord(c) >= 128}
    freq = Counter(c for c in donor_text if ord(c) >= 128)
    three, two = defaultdict(list), defaultdict(list)
    for c in sorted(vocab, key=lambda x: -freq[x]):
        b = c.encode("utf-8")
        if len(b) == 3 and 0x81 <= b[2] <= 0xBF:
            # a 0x80 tail would have left a euro sign and no marker at all
            three[b[:2].hex().upper()].append(c)
        elif len(b) == 2:
            two[b[:1].hex().upper()].append(c)
    return freq, {"3": three, "2": two}


def _candidates(by_prefix, buf):
    """Candidates for the character a marker is completing."""
    if len(buf) >= 2 and 0xE0 <= buf[0] <= 0xEF:
        return "3", by_prefix["3"].get("%02X%02X" % (buf[0], buf[1]), [])
    if len(buf) == 1 and 0xC0 <= buf[0] <= 0xDF:
        return "2", by_prefix["2"].get("%02X" % buf[0], [])
    return "?", []


NAME_OF = {
    "sqrt": "\u221a", "in": "\u2208", "arrow": "\u2192", "down": "\u2193",
    "emdash": "\u2014", "endash": "\u2013", "check": "\u2705", "cross": "\u2717",
    "tick": "\u2713", "warn": "\u26a0", "approx": "\u2248", "identical": "\u2261",
    "bar": "\u2502", "tee": "\u251c", "elbow": "\u2514", "hline": "\u2500",
    "inf": "\u221e", "partial": "\u2202", "minus": "\u2212", "hourglass": "\u23f3",
}


def name_of(token):
    return NAME_OF.get(token, token)


BOX_VERTICAL = "\u2502"
BOX_TEE = "\u251c"
BOX_L = "\u2514"
BOX_H = "\u2500"
BOX_PREFIX = "E294"
# A tree row nested under a directory that still has siblings starts with the
# vertical bar; a top-level row starts with a tee or an elbow.
BOX_TEE_SET = (BOX_TEE, BOX_L)


# --------------------------------------------------------------------------
# inversion
# --------------------------------------------------------------------------
def reconstruct(body, by_prefix, marker_idx, decisions, eaten=" ", line_no=0, pins=None):
    """Rebuild one damaged line from its byte stream.

    Only CJK and U+20AC are pipe products; every other non-ASCII character in
    the line is itself, because a line can be partly damaged and still hold
    real arrows and em dashes.  GBK-encoding those back would destroy them.

    marker_idx  indices of the '?' that are markers rather than real question
                marks (slots() works that out)
    decisions   list that structural judgements are appended to, so they can be
                reported instead of applied silently
    eaten       the character the marker swallowed.  It is a space almost
                everywhere, but a marker that lands on the last symbol of a
                CRLF line swallows the CR instead -- visible as a damaged line
                that has no trailing CR while the rest of the file does.
    pins        {(line, width, prefix): char} from --symbol
    """
    stream = []
    for i, ch in enumerate(body):
        if ch == EURO:
            stream.append(0x80)          # single byte; UTF-8 would add two
        elif is_cjk(ch):
            try:
                stream.extend(ch.encode("gbk"))
            except UnicodeEncodeError:
                stream.extend(ch.encode("utf-8"))
        elif i in marker_idx and ch == "?":
            stream.append(None)
        elif ord(ch) >= 128:
            stream.extend(ch.encode("utf-8"))
        else:
            stream.extend(ch.encode("utf-8"))

    def seql(b):
        return seqlen(b)

    pieces, buf = [], bytearray()
    for b in stream:
        if b is None:
            width, cands = _candidates(by_prefix, buf)
            key = ("%02X" % buf[0]) if (width == "2" and buf) else (
                "%02X%02X" % (buf[0], buf[1]) if len(buf) >= 2 else "?")
            sym = None
            for scope in (line_no, 0):          # a row pin beats a file pin
                pin = (pins or {}).get((scope, width, key))
                if pin:
                    want = name_of(pin)
                    if want in cands:
                        sym = want
                        decisions.append(("pin", key, want, cands))
                        break
            if sym is None and cands:
                # ONE structural judgement in the whole tool: a tree row that
                # starts at column 0 and is not the file's only row shape gets
                # the vertical bar.  Reported, never silent.
                sym = (BOX_VERTICAL if (width == "3" and cands[0] != BOX_VERTICAL
                                        and len(cands) > 1 and buf[0] == 0xE2
                                        and cands[0] in BOX_TEE_SET)
                       else cands[0])
                decisions.append((width, key, sym, cands))
            if sym is None:
                sym = "\ufffd"
            pieces.append(sym)
            pieces.append(eaten)        # the character the marker ate
            buf.clear()
            continue
        buf.append(b)
        while buf:
            need = seql(buf[0])
            if len(buf) < need:
                break
            try:
                pieces.append(bytes(buf[:need]).decode("utf-8"))
            except UnicodeDecodeError:
                pieces.append("\ufffd")
            del buf[:need]
    return "".join(pieces)


# --------------------------------------------------------------------------
# the acceptance gate -- nothing is written unless this passes
# --------------------------------------------------------------------------
def run_bytes(s, i, j):
    out, holes = [], 0
    for k in range(i, j):
        ch = s[k]
        if ch == EURO:
            out.append(0x80)
        elif is_cjk(ch):
            try:
                out.extend(ch.encode("gbk"))
            except UnicodeEncodeError:
                out.extend(ch.encode("utf-8"))
        elif ch == "?":
            out.append(None)             # one entry == two unknown bytes
            holes += 1
        else:
            out.extend(ch.encode("utf-8"))
    return out, holes


def bytes_match(pattern, actual):
    di = fi = 0
    for b in pattern:
        if b is None:
            fi += 2
            continue
        if fi >= len(actual) or actual[fi] != b:
            return False
        di += 1
        fi += 1
    return fi == len(actual)


def gate_line(damaged, fixed):
    """Byte-level acceptance for one line, or None if it passes.

    A damaged run must reproduce its observed bytes (a marker standing for
    exactly two lost bytes); an undamaged line must be byte-identical, so a
    repair can never invent content.  The ASCII text must match throughout.

    The CR is deliberately NOT stripped.  When a marker lands on the last
    symbol of a row it swallows the CR, so the damaged row has no terminator
    while the repaired one does -- and that terminator is the second of the two
    lost bytes.  Stripping both sides first makes the byte counts disagree by
    one and the row can never pass."""
    d, f = damaged, fixed
    if not line_damaged(d):
        return None if f == d else "undamaged line was modified"
    i = j = 0
    while i < len(d) and j < len(f):
        if is_damage_run(d, i):
            k = i
            while k < len(d) and is_damage_run(d, k):
                k += 1
            pat, holes = run_bytes(d, i, k)
            m = j
            while m < len(f) and ord(f[m]) >= 128:
                m += 1
            target = len(pat) + holes     # each None expands to two bytes
            while m < len(f) and len(f[j:m].encode("utf-8")) < target:
                m += 1
            if not bytes_match(pat, f[j:m].encode("utf-8")):
                return "damage run at %d does not reproduce (%d hole(s))" % (i, holes)
            i, j = k, m
        else:
            if d[i] != f[j]:
                return "ascii text differs at %d" % i
            i += 1
            j += 1
    if i < len(d) or j < len(f):
        return "length mismatch"
    return None


def gate(damaged_lines, fixed_lines):
    bad = []
    for n, (d, f) in enumerate(zip(damaged_lines, fixed_lines), 1):
        why = gate_line(d, f)
        if why:
            bad.append((n, why))
    return bad


# --------------------------------------------------------------------------
# donor alignment, for the lines whose original text still exists somewhere
# --------------------------------------------------------------------------
def canon(line):
    """Corruption-insensitive form: mask non-ASCII and damage on BOTH sides
    (the donor is clean, so its symbols must be masked too or the two sides
    can never compare equal), collapse runs, and drop spaces next to a marker
    because that is the space the marker ate.  Trailing CR is dropped: the
    donor is LF and the working tree is CRLF."""
    line = line.rstrip("\r")
    bad = slots(line)
    red = []
    for i, c in enumerate(line):
        if i in bad or ord(c) >= 128:
            if not red or red[-1] != WILD:
                red.append(WILD)
        else:
            red.append(c)
    res = []
    for k, c in enumerate(red):
        if c == " ":
            prev = red[k - 1] if k > 0 else ""
            nxt = red[k + 1] if k + 1 < len(red) else ""
            if prev == WILD or nxt == WILD:
                continue
        res.append(c)
    return "".join(res)


WILD = "\x00"


def splice_donor(damaged_lines, donor_lines, crlf=True):
    """Return (lines, stats).  A donor line is accepted only when running it
    forward through the corruption reproduces the damaged line EXACTLY.

    An earlier gate compared the GBK prefix multiset, which is necessary but not
    sufficient: two near-identical table rows share a skeleton and a symbol set,
    and the wrong one slips through.  The end-to-end test caught that -- it
    restored two rows the byte-level gate then refused.  Re-deriving the damage
    is cheap and leaves no room for that class of mistake.

    The donor comes out of git LF-normalised, so the terminator is re-attached
    from the file's own convention.  It cannot be taken from the damaged row:
    when a marker swallows the CR, that row has no terminator to copy, and the
    spliced row would come back without one and never match its own bytes.
    """
    index = defaultdict(list)
    for i, dl in enumerate(donor_lines):
        c = canon(dl)
        if len(c.replace(WILD, "").strip()) >= 6:
            index[c].append(i)

    term = "\r" if crlf else ""
    out = list(damaged_lines)
    spliced_at = {}
    taken, ambiguous, unmatched = 0, 0, 0
    for ci, cl in enumerate(damaged_lines):
        if not line_damaged(cl):
            continue
        want = cl.rstrip("\r")
        hits = [
            di for di in index.get(canon(cl), [])
            if corrupt(donor_lines[di].rstrip("\r").encode("utf-8")) == want
        ]
        if len(hits) == 1:
            out[ci] = donor_lines[hits[0]].rstrip("\r") + term
            spliced_at[ci] = hits[0]
            taken += 1
        elif len(hits) > 1:
            ambiguous += 1
        else:
            unmatched += 1
    return out, {"spliced": taken, "ambiguous": ambiguous,
                 "unmatched": unmatched, "at": spliced_at}


def _gbk_prefixes_unused():
    """Kept out of the tool: the prefix multiset was the first splice gate and
    it is not sufficient.  See splice_don's docstring."""


# --------------------------------------------------------------------------
# git plumbing
# --------------------------------------------------------------------------
def git_blob(rev_path):
    # --no-pager: `git show` opens a pager and waits for input when its output
    # is a pipe, which hangs the caller with no output and no error.
    p = subprocess.run(
        ["git", "--no-pager", "-C", REPO, "cat-file", "blob", rev_path],
        capture_output=True,
    )
    if p.returncode != 0:
        return None
    return p.stdout


def read_target(rel):
    raw = open(os.path.join(REPO, rel), "rb").read()
    has_bom = raw[:3] == b"\xef\xbb\xbf"
    return raw, raw.decode("utf-8-sig"), has_bom


def apply_overrides(by_prefix, pins):
    """Validate every pin against the donor's candidates.

    Only a FILE-level pin (no row number) reorders the shared candidate table.
    A row pin must not: reordering the table leaks the choice into every other
    row that shares the prefix, which is how pinning row 76 to the infinity
    sign would quietly turn four unrelated set-membership signs into infinity
    signs.  Row pins are looked up per row in reconstruct() instead.
    """
    for (line, width, prefix), want in pins.items():
        table = by_prefix[width]
        pool = table.get(prefix, [])
        hit = [c for c in pool if c == want or c == name_of(want)]
        if not hit:
            raise SystemExit(
                "--symbol %s:%s=%s: not one of %s"
                % (line or "*", prefix, want,
                   ", ".join("U+%04X" % ord(c) for c in pool) or "no candidates for that prefix"))
        if line == 0:
            table[prefix] = [hit[0]] + [c for c in pool if c != hit[0]]


# --------------------------------------------------------------------------
# modes
# --------------------------------------------------------------------------
SELFTEST_LINES = [
    "# snn_mbt \u2014 MoonBit port of SpikingNeuralNetworks.jl",
    "| Unit system | \u2705 done | 30+ Float32 unit constants |",
    "| **festa2024.mbt** | \u26a0 partial | scaled 10\u00d7 down |",
    "    \u251c\u2500\u2500 moon.mod                     # module: riantr/snn_mbt",
    "\u2502   \u251c\u2500\u2500 main.mbt             # port of chain.jl",
    "\u2502   \u2514\u2500\u2500 moon.pkg",
    "\u2514\u2500\u2500 _build/                      # moon build artifacts",
    "- `delay_dist` \u2014 \u2705 done in v0.10.3",
    "weights Normal(0, \u03bc/\u221apN))",
    "Steady-state \u2248 0.21, u \u2192 .907, 885.6 \u2192 030.8 |",
    "Float32 \u03bb; mean verified \u2248 \u03bb (3 tests pass)",
    "t.pipe(width?, height?)   # genuine question marks, not markers",
]


def cmd_selftest(args, out):
    """Round-trip a set of clean lines and require an exact recovery.

    This is the check that would have caught the guessed reverse mapping: it
    runs the same corruption the file went through, then the same inversion
    the repair uses."""
    vocab_src = "".join(SELFTEST_LINES) + "".join(
        c for c in "".join(SELFTEST_LINES) if ord(c) >= 128
    )
    _, by_prefix = build_vocab(vocab_src)
    fails = 0
    for line in SELFTEST_LINES:
        dmg = corrupt(line.encode("utf-8"))
        if not has_cjk(dmg) and EURO not in dmg:
            # nothing to invert; make sure that is genuinely the case
            out.write("  clean (no damage): %s\n" % line[:58])
            continue
        structural = []
        got = reconstruct(dmg, by_prefix, slots(dmg), structural)
        if got != line:
            fails += 1
            out.write("  FAIL %r\n       damaged  %r\n       recovered %r\n" % (line, dmg, got))
        else:
            out.write("  ok   %-58s  <- %s\n" % (line[:58], dmg[:28].replace("\n", " ")))
    out.write("selftest: %d case(s), %d failure(s)\n" % (len(SELFTEST_LINES), fails))
    return EXIT_OK if fails == 0 else EXIT_GATE


def cmd_check(args, out):
    raw, text, has_bom = read_target(args.target)
    lines = text.split("\n")
    damaged = [i for i, l in enumerate(lines, 1) if line_damaged(l)]
    cjk = sum(1 for c in text if is_cjk(c))
    euros = text.count(EURO)
    markers = sum(1 for l in lines for i in slots(l) if l[i] == "?")
    out.write("file        : %s\n" % args.target)
    out.write("bytes       : %d   BOM: %s   lines: %d\n" % (len(raw), has_bom, len(lines)))
    out.write("CJK         : %d\n" % cjk)
    out.write("euro signs  : %d\n" % euros)
    out.write("markers '?' : %d\n" % markers)
    out.write("genuine '?' : %d  (question marks in the text, e.g. ascii_plot(width?, height?))\n"
              % (text.count("?") - markers))
    out.write("damaged rows: %d\n" % len(damaged))
    if damaged:
        out.write("VERDICT: DAMAGED -- a GBK round trip has touched this file.\n")
        for i in damaged[:10]:
            out.write("   line %d: %s\n" % (i, lines[i - 1].strip()[:70]))
        if len(damaged) > 10:
            out.write("   ... and %d more\n" % (len(damaged) - 10))
        return EXIT_DAMAGE
    out.write("VERDICT: CLEAN\n")
    return EXIT_OK


def cmd_repair(args, out):
    rel = args.target
    donor_rel = args.donor_path or rel
    raw, text, has_bom = read_target(rel)
    lines = text.split("\n")
    if not any(line_damaged(l) for l in lines):
        out.write("nothing to repair: %s is already clean.\n" % rel)
        return EXIT_OK

    donor_rev = args.donor
    donor_raw = None
    if donor_rev is None:
        donor_raw = git_blob("HEAD:%s" % donor_rel)
        if donor_raw is not None:
            donor_text = donor_raw.decode("utf-8-sig")
            if not any(line_damaged(l) for l in donor_text.split("\n")):
                donor_rev = "HEAD"
                out.write("donor       : HEAD (clean)\n")
            else:
                donor_raw = None
    if donor_raw is None:
        if donor_rev is None:
            out.write("ERROR: no donor. HEAD is also damaged, so pass a clean\n"
                      "       revision explicitly:  --donor <rev>\n")
            return EXIT_USAGE
        donor_raw = git_blob("%s:%s" % (donor_rev, donor_rel))
        if donor_raw is None:
            out.write("ERROR: cannot read donor %s:%s\n" % (donor_rev, donor_rel))
            return EXIT_USAGE
        out.write("donor       : %s\n" % donor_rev)
    donor_text = donor_raw.decode("utf-8-sig")
    donor_lines = donor_text.split("\n")
    if any(line_damaged(l) for l in donor_lines):
        out.write("ERROR: donor is itself damaged; refusing to repair from it.\n")
        return EXIT_USAGE

    freq, by_prefix = build_vocab(donor_text)
    pins = parse_symbol_pins(args.symbol)
    apply_overrides(by_prefix, pins)
    for (line, width, prefix), want in sorted(pins.items()):
        out.write("pinned      : %s%s -> %s\n"
                  % ("row %d " % line if line else "", prefix, name_of(want)))

    crlf_majority = sum(1 for l in lines if l.endswith("\r")) * 2 > len(lines)
    stage1, stats = splice_donor(lines, donor_lines, crlf=crlf_majority)
    out.write("stage 1 (donor splice): %d row(s) restored byte-exactly, "
              "%d ambiguous, %d without a usable donor row\n"
              % (stats["spliced"], stats["ambiguous"], stats["unmatched"]))

    structural, fixed = [], []
    judgement = []
    lost_cr_rows = 0
    for ci, cl in enumerate(stage1):
        line_no = ci + 1
        cr = cl.endswith("\r")
        body = cl.rstrip("\r")
        if not any(is_cjk(c) for c in body):
            fixed.append(cl)          # undamaged: pass through untouched
            continue
        judgement.append(ci + 1)     # no donor row: this is a judgement
        # A marker landing on the last symbol of a line swallows the CR, not a
        # space.  That shows up as a damaged row with no trailing CR while the
        # rest of the file has one.
        lost_cr = crlf_majority and not cr
        if lost_cr:
            lost_cr_rows += 1
        new = reconstruct(body, by_prefix, slots(body), structural,
                               "" if lost_cr else " ", line_no=line_no, pins=pins)
        # The marker swallowed the CR, so the terminator is restored here rather
        # than as the eaten character; either way the repaired row ends in one.
        fixed.append(new + ("\r" if crlf_majority else ""))

    symbol_sites = len(structural)
    cjk_left = sum(1 for c in "\n".join(fixed) if is_cjk(c))
    changed = sum(1 for a, b in zip(lines, fixed) if a != b)
    out.write("stage 2 (byte stream) : %d symbol slot(s) resolved from the "
              "surviving 2-byte prefix\n" % symbol_sites)
    out.write("rows changed          : %d   CJK remaining: %d   rows whose CR "
              "the marker swallowed: %d\n" % (changed, cjk_left, lost_cr_rows))

    if structural:
        out.write("\nstructural judgements (the one place this tool is not "
                  "proving itself -- please look):\n")
        seen = Counter((w, p, s) for w, p, s, _ in structural)
        cands_of = {}
        for w, p, s, cands in structural:
            cands_of.setdefault((w, p, s), cands)
        for (width, prefix, sym), n in sorted(seen.items()):
            out.write("   %s-byte %-5s -> %s U+%04X  x%d  (candidates: %s)\n"
                      % (width, prefix, sym, ord(sym), n,
                         ", ".join("U+%04X" % ord(c) for c in cands_of[(width, prefix, sym)])))

    out.write("\nJUDGEMENT ZONE  %d row(s) the donor could not supply, so the "
              "symbol and the swallowed character\n"
              "                 are inferred rather than proven.  These are the "
              "rows to read:\n" % len(judgement))
    out.write("   %s\n" % (", ".join(str(n) for n in judgement) or "(none)"))

    bad = gate(lines, fixed)
    out.write("\nACCEPTANCE GATE  %s\n" % ("PASS" if not bad else "FAIL"))
    out.write("  every damage run reproduces its observed bytes, a marker "
              "counting as two lost ones\n")
    out.write("  every undamaged row is byte-identical\n")
    out.write("  the ASCII text is untouched\n")
    if bad:
        for n, why in bad[:20]:
            out.write("  L%d  %s\n" % (n, why))
        out.write("  %d violation(s); NOT writing.\n" % len(bad))
        return EXIT_GATE
    out.write("  0 violations\n")

    if args.dry_run:
        out.write("\n--dry-run: gate passed, nothing written.\n")
        return EXIT_OK

    body = "\n".join(fixed)
    blob = (b"\xef\xbb\xbf" if has_bom else b"") + body.encode("utf-8")
    with open(os.path.join(REPO, rel), "wb") as fh:
        fh.write(blob)
    out.write("\nwrote %s (%d bytes, BOM %s, %d CRLF lines)\n"
              % (rel, len(blob), has_bom, body.count("\r\n")))
    out.write("review the diff, then commit. The gate passed, but the "
              "structural judgements above are still a human's call.\n")
    return EXIT_OK


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="readme_encoding_guard.py",
        description="Detect, and optionally undo, a GBK round trip over a "
                    "markdown file (default: %s)." % REL_DEFAULT,
    )
    ap.add_argument("mode", nargs="?", default="check",
                    choices=("check", "repair", "selftest"))
    ap.add_argument("--target", default=REL_DEFAULT, metavar="PATH",
                    help="file to inspect or repair, relative to the repo root")
    ap.add_argument("--donor", metavar="REV",
                    help="clean revision to restore original text from "
                         "(default: HEAD, if HEAD is clean)")
    ap.add_argument("--symbol", action="append", metavar="[LINE:]PREFIX=CHAR",
                    help="pin a symbol, e.g. --symbol 77:E288=sqrt; repeatable. "
                         "Prefix-only pins are too coarse when one prefix "
                         "carries more than one meaning.")
    ap.add_argument("--donor-path", metavar="PATH",
                    help="path of the donor inside the revision (default: the "
                         "target path). Set it when repairing a copy under "
                         "_build/ so the donor still comes from the repo.")
    ap.add_argument("--dry-run", action="store_true",
                    help="run the full repair and the gate, but write nothing")
    args = ap.parse_args(argv)
    out = io.StringIO()
    rc = {"check": cmd_check, "repair": cmd_repair, "selftest": cmd_selftest}[args.mode](args, out)
    sys.stdout.write(out.getvalue())
    sys.stdout.flush()
    return rc


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        sys.exit(EXIT_USAGE)
