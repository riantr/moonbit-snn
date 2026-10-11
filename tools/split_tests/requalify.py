#!/usr/bin/env python3 -X utf8
"""Qualify references from the root package into a sub-package.

The rewriter in `split_qualify.py` goes the other way: it rewrites a file so
its references to the *root* package carry `@snn_mbt.`. Once part of the root
has moved into a sub-package, the remaining root files have the opposite need
-- their references to that moved code must carry `@<subpkg>.`.

Two things make this different enough to be its own pass rather than a flag:

- The allow-list is the moved files' own symbols, not `pkg.generated.mbti`.
  Once a file leaves the root, the .mbti no longer describes the root's own
  surface accurately.
- A name defined in *both* places needs the root meaning inside the root. The
  three such collisions in this tree (`IF`, `SpikingAttention`, `Monitor`) are
  reported by `--list-collisions` rather than silently qualified.

Usage:
    python requalify.py --to core --dry-run
    python requalify.py --to core --apply
    python requalify.py --to core --list-collisions
"""
import argparse
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from split_qualify import (  # noqa: E402
    load_own_symbols, shadowed_names, split_code, _named_argument_labels,
    _span_to, decl_line_starts, _split_decl, _rewrite,
)

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
MBT = os.environ.get("SNN_MBT_DIR", os.path.join(REPO, "mbt"))


def root_files():
    """Top-level .mbt files of the root package (not the sub-packages)."""
    out = []
    for f in sorted(glob.glob(os.path.join(MBT, "*.mbt"))):
        out.append(os.path.basename(f))
    return out


def sub_files(pkg):
    return sorted(os.path.basename(f)
                  for f in glob.glob(os.path.join(MBT, pkg, "*.mbt")))


def symbols_of(files, base=MBT):
    """name -> defining file, for the given basenames.

    `base` matters: the two lists produced here have different homes. Root
    files sit directly in the package directory, sub-package files sit one
    level down, and passing a bare basename for the latter silently resolves
    to nothing -- which showed up as "0 files, 0 prefixes" rather than as an
    error, because the tool had simply found no symbols to qualify.
    """
    owner = {}
    for f in files:
        p = os.path.join(base, f)
        if not os.path.exists(p):
            continue
        for s in load_own_symbols(open(p, encoding="utf-8").read()):
            owner.setdefault(s, f)
    return owner


def requalify(src, names, prefix):
    """Prefix every reference to `names` that is not a definition."""
    shadow = shadowed_names(src) | _named_argument_labels(src)
    bare = {n for n in names if n not in shadow}
    order = sorted(bare, key=len, reverse=True)
    t_pat = "|".join(re.escape(n) for n in order if n[:1].isupper())
    b_pat = "|".join(re.escape(n) for n in order)

    segments = split_code(src)
    if "".join(t for _, t in segments) != src:
        raise AssertionError("split_code is not lossless")

    def_spans = _span_to(src, decl_line_starts(src))
    stats = {"type_method": 0, "bare": 0, "def_skipped": 0}
    out = []
    pos = 0
    for is_code, text in segments:
        seg_lo, seg_hi = pos, pos + len(text)
        pos = seg_hi
        if not is_code:
            out.append(text)
            continue
        cursor = seg_lo
        for sp_lo, sp_hi in def_spans:
            if sp_lo < cursor or sp_lo >= seg_hi:
                continue
            out.extend(_rewrite(text[cursor - seg_lo:sp_lo - seg_lo],
                                t_pat, b_pat, prefix, stats))
            head, tail = _split_decl(text[sp_lo - seg_lo:sp_hi - seg_lo])
            stats["def_skipped"] += 1
            out.append(head)
            out.extend(_rewrite(tail, t_pat, b_pat, prefix, stats))
            cursor = sp_hi
        out.extend(_rewrite(text[cursor - seg_lo:], t_pat, b_pat, prefix,
                            stats))
    return "".join(out), stats


def requalify_examples(pkg, sub_owner):
    """Repoint already-qualified references in examples and tests.

    Root-package consumers write `@snn_mbt.Xoshiro`. That stops resolving once
    `Xoshiro` moves out, because the root does not re-export its sub-packages
    -- a probe confirmed `moon info` lists no sub-package symbol in the root
    .mbti. So each consumer that names a moved symbol has to import the
    sub-package and use its qualifier.

    Only the qualifier's owner changes; the identifier is untouched, and
    already-correct `@core.` references are skipped so the pass is re-runnable.
    """
    moved = set(sub_owner)
    base_prefix = "@snn_mbt."
    new_prefix = "@" + pkg + "."
    touched = []
    total = 0
    for pat in (os.path.join(MBT, "examples", "*", "*.mbt"),
                os.path.join(MBT, "tests", "*", "*.mbt")):
        for f in sorted(glob.glob(pat)):
            src = open(f, encoding="utf-8", newline="").read()
            names = set(re.findall(re.escape(base_prefix) + r'(\w+)', src))
            hit = names & moved
            if not hit:
                continue
            def sub(m):
                nonlocal total
                if m.group(1) in moved:
                    total += 1
                    return new_prefix + m.group(1)
                return m.group(0)
            new = re.sub(re.escape(base_prefix) + r'(\w+)', sub, src)
            open(f, "w", encoding="utf-8", newline="").write(new)
            touched.append((os.path.relpath(f, MBT), len(hit)))
    return touched, total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--to", required=True,
                    help="sub-package directory name under mbt/")
    ap.add_argument("--apply", action="store_true",
                    help="write the result (default is a dry run)")
    ap.add_argument("--list-collisions", action="store_true",
                    help="report names defined both in the root and the "
                         "sub-package, then exit")
    ap.add_argument("--examples", action="store_true",
                    help="also repoint @snn_mbt.X in examples/ and tests/ "
                         "for the symbols that moved")
    args = ap.parse_args()

    pkg = args.to
    prefix = "@" + pkg + "."

    roots = root_files()
    subs = sub_files(pkg)
    if not subs:
        print(f"ERROR: no .mbt under {os.path.join(MBT, pkg)}")
        return 1

    sub_owner = symbols_of(subs, os.path.join(MBT, pkg))
    root_owner = symbols_of(roots)

    if args.list_collisions:
        both = sorted(set(sub_owner) & set(root_owner))
        print(f"names defined in both the root and {pkg}/: {len(both)}")
        for n in both:
            print(f"  {n:<28} root={root_owner[n]:<26} {pkg}={sub_owner[n]}")
        return 0

    # A name the root also defines must keep its root meaning inside the root.
    both = set(sub_owner) & set(root_owner)
    targets = {n: f for n, f in sub_owner.items() if n not in both}

    total = 0
    changed = 0
    for f in roots:
        p = os.path.join(MBT, f)
        src = open(p, encoding="utf-8", newline="").read()
        new, st = requalify(src, set(targets), prefix)
        n = st["type_method"] + st["bare"]
        if n:
            total += n
            changed += 1
            print(f"  {f:<42} {n:>4} prefixes")
            if args.apply:
                open(p, "w", encoding="utf-8", newline="").write(new)
    print(f"\n{changed} files, {total} prefixes "
          f"-> {prefix}*  ({'applied' if args.apply else 'dry run'})")
    if both:
        print(f"\nskipped {len(both)} colliding name(s), they stay "
              f"root-qualified inside the root: {', '.join(sorted(both))}")

    if args.examples:
        touched, n = requalify_examples(pkg, sub_owner)
        for f, k in touched:
            print(f"  {f:<52} {k} symbols")
        print(f"\nexamples/tests: {n} references repointed "
              f"to {prefix}*  ({'applied'})")
        print(f"each of those packages also needs "
              f'"riantr/snn_mbt/{pkg}" in its moon.pkg import block')
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)