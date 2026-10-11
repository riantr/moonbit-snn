#!/usr/bin/env python3 -X utf8
"""Materialise a wave-N test sub-package.

Copies the selected root-package `*_test.mbt` files into
`mbt/tests/<wave>/` with `@snn_mbt.` qualification applied. The originals are
LEFT IN PLACE — deleting them is a separate, deliberate step, and only after
`moon test ./tests/<wave>` has proved the copies work. Deleting first and
discovering a missing prefix afterwards loses the reference.

Usage:
    python package_wave.py --wave wave3 --pick _build/pick_wave3.txt
    python package_wave.py --wave wave3 --pick -            # read stdin
    python package_wave.py --wave wave3 --dry-run           # count only

The pick file is one filename per line. Blank lines and `#` comments are
ignored. Paths may be given relative to the package root.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from split_qualify import load_public, qualify  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
MBT = os.environ.get("SNN_MBT_DIR", os.path.join(REPO, "mbt"))

MOON_PKG = """// moon.pkg for the {wave} test sub-package.
//
// Every test file in here was moved out of the root package so that the root
// package's test target no longer has to codegen as a single C translation
// unit. Access to the package under test is via the blackbox import below;
// `moon.pkg` (not `moon.pkg.json`) is the per-directory manifest name.
//
// The import is marked `for "test"` rather than unconditional. An
// `is-test: true` package already receives every public symbol of the package
// under test through test-import-all, so the plain form left moon reporting
// `Unused package 'riantr/snn_mbt'` on all nine wave manifests even though the
// files qualify hundreds of references as `@snn_mbt.X`. The qualified form is
// what moonbit-community/rabbita does for the same reason.
//
// Third-party packages are a separate, unconditional block{third_party_note}.

import {{
  "riantr/snn_mbt",
}} for "test"
{extra}
options(
  "is-test": true,
)
"""

# A test may reach a third-party package directly (`@moonbit_image.decode`).
# The root package declares those in its own moon.pkg, but a sub-package does
# NOT inherit them — `moon.pkg` is not inherited by nested directories — so a
# migrated test that used one keeps the `@moonbit_image.` qualifier and fails
# with `Package "moonbit_image" not found` unless the import is repeated here.
EXTERNAL = re.compile(r'@(moonbit_image|sqlite3)\.')


def _third_party_block(lines):
    """The unconditional import block for third-party packages, or empty."""
    if not lines:
        return ""
    return "\nimport {\n" + "".join(lines) + "}\n"


def _third_party_note(lines):
    """A prose pointer in the header comment, or nothing when unused."""
    if not lines:
        return ""
    names = ", ".join(l.strip().strip('",') for l in lines)
    return f" (this wave also uses {names})"

_IMPORT_BLOCK = re.compile(r'import\s*\{([^}]*)\}', re.S)
_IMPORT_ENTRY = re.compile(r'"([^"]+)"')


def _root_imports():
    """Import paths the root package already declares, keyed by short name.

    The qualifier in code (`@moonbit_image`) is the last path segment, but the
    import needs the full module path (`riantr/moonbit_image`) — declaring the
    bare name fails with `Cannot find import 'moonbit_image'`. Reading the
    root manifest keeps the two in step instead of hardcoding a mapping that
    would rot on the next dependency bump.
    """
    mapping = {}
    pkg = os.path.join(MBT, "moon.pkg")
    if not os.path.exists(pkg):
        return mapping
    with open(pkg, encoding="utf-8") as fh:
        for block in _IMPORT_BLOCK.findall(fh.read()):
            for path in _IMPORT_ENTRY.findall(block):
                mapping[path.rsplit("/", 1)[-1]] = path
    return mapping


def external_imports_from_dir(directory):
    """Same detection, but over files already living in the sub-package."""
    names = [n for n in os.listdir(directory) if n.endswith(".mbt")]
    return external_imports(names, base=directory)


def external_imports(names, base=None):
    """Third-party import lines needed by the given filenames."""
    root = _root_imports()
    src_dir = base or MBT
    found = []
    for name in names:
        with open(os.path.join(src_dir, name), encoding="utf-8",
                  newline="") as fh:
            for m in EXTERNAL.finditer(fh.read()):
                short = m.group(1)
                if short not in found:
                    found.append(short)
    lines = []
    unknown = []
    for short in found:
        full = root.get(short)
        if full:
            lines.append(f'  "{full}",\n')
        else:
            unknown.append(short)
    if unknown:
        raise SystemExit(
            f"ERROR: these third-party packages are used but not declared in "
            f"{os.path.join(MBT, 'moon.pkg')}: {', '.join(unknown)}")
    return lines


def read_pick(spec):
    """Return the filenames listed in `spec`, or in stdin when it is `-`."""
    if spec == "-":
        raw = sys.stdin.read()
    else:
        with open(spec, encoding="utf-8-sig") as fh:
            raw = fh.read()
    out = []
    for line in raw.splitlines():
        name = line.strip()
        if not name or name.startswith("#"):
            continue
        out.append(os.path.basename(name))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wave", required=True, help="sub-package name, e.g. wave3")
    ap.add_argument("--pick", required=True,
                    help="file listing the test filenames, or - for stdin")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would be written without writing")
    ap.add_argument("--rewrite-pkg", action="store_true",
                    help="only refresh moon.pkg; sources are already moved")
    args = ap.parse_args()

    dst = os.path.join(MBT, "tests", args.wave)
    picks = read_pick(args.pick)

    # After a wave has been committed its tests live only in the sub-package,
    # so re-running the packer over the original pick list finds nothing. This
    # mode refreshes just the manifest, which is where the import form changed.
    if args.rewrite_pkg:
        os.makedirs(dst, exist_ok=True)
        ext = external_imports_from_dir(dst)
        with open(os.path.join(dst, "moon.pkg"), "w", encoding="utf-8",
                  newline="") as fh:
            fh.write(MOON_PKG.format(
                wave=args.wave, extra=_third_party_block(ext),
                third_party_note=_third_party_note(ext)))
        print(f"rewrote {os.path.join(dst, 'moon.pkg')}")
        if ext:
            print(f"  third-party imports: {len(ext)}")
        return 0

    missing = [n for n in picks if not os.path.exists(os.path.join(MBT, n))]
    if missing:
        print(f"ERROR: {len(missing)} listed file(s) not found in {MBT}:")
        for n in missing[:20]:
            print(f"  {n}")
        return 1

    types, free_fns, values = load_public()
    grand = 0
    if not args.dry_run:
        os.makedirs(dst, exist_ok=True)
        ext = external_imports(picks)
        if ext:
            print(f"  third-party imports needed: {', '.join(ext)}")
        with open(os.path.join(dst, "moon.pkg"), "w", encoding="utf-8",
                  newline="") as fh:
            fh.write(MOON_PKG.format(
                wave=args.wave, extra=_third_party_block(ext),
                third_party_note=_third_party_note(ext)))

    for name in picks:
        # newline='' so CRLF/LF in the original survives byte-for-byte
        with open(os.path.join(MBT, name), encoding="utf-8", newline="") as fh:
            src = fh.read()
        new, st = qualify(src, types, free_fns, values)
        n = st["type_method"] + st["bare"]
        grand += n
        if not args.dry_run:
            with open(os.path.join(dst, name), "w", encoding="utf-8",
                      newline="") as fh:
                fh.write(new)
        print(f"  {name:<46} {n:>4} prefixes")

    verb = "would write" if args.dry_run else "wrote"
    print(f"\n{len(picks)} files {verb} -> {dst}")
    if args.dry_run:
        return 0
    print(f"total prefixes = {grand}")
    rel = "./" + os.path.relpath(dst, MBT).replace(os.sep, "/")
    print("\n原文件保留在根包未删。下一步：")
    print(f"  1) moon test {rel} --target native")
    print("  2) 全绿后再删除根目录原文件，最后跑 moon check 确认 0 errors")
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)