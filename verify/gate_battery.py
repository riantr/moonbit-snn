# gate_battery.py -- prove that the gates in qa_loop.py can FAIL.
#
# A gate that has only ever been green measures nothing. This applies the
# registry one gate at a time: each gate gets a deliberate fault injected
# into the thing it MEASURES, the gate is run, and it has to go RED. The
# fault is then removed and the gate is run again, and it has to go GREEN
# again -- so the red is attributable to the fault, and not to the machine,
# the clock, or a gate that was already broken.
#
# The faults are restored BYTE-EXACTLY. This repository has
# core.autocrlf=true and no .gitattributes, so restoring a file with
# `git checkout --` would rewrite it with CRLF and change its bytes: a
# byte-comparison gate would then fail on a tree that nothing had actually
# changed. The snapshot is taken in memory and written back as it was.
#
# Run:  python verify/gate_battery.py            # every gate with a fault
#       python verify/gate_battery.py lint fuzz  # only these
#
# The exit code is 0 only when every gate went red under its fault and
# green again afterwards. A gate that cannot be made to fail is a finding,
# not a pass.

import os
import subprocess
import sys
import time

# This machine's console codec is GBK, and the output of a failing gate can
# contain a character GBK cannot encode -- the replacement character from a
# decode that went wrong. Printing that raises UnicodeEncodeError and kills
# the battery halfway through, which is the worst possible moment: the gates
# before the crash have been proved and the ones after have not.
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QA = os.path.join(ROOT, "verify", "qa_loop.py")

# Each fault: the gate, what the fault is, and the edits that make it.
# An edit is (path, old, new); `old` of None means APPEND `new` to the file.
FAULTS = [
    ("lint",
     "a type error in a library file",
     [("mbt/core/util.mbt", None,
       'let qa_battery_probe : Int = "not an Int"')]),

    ("integration",
     "a type error in one of the 77 examples",
     [("mbt/examples/chain/main.mbt", None,
       'let qa_battery_probe : Int = "not an Int"')]),

    ("coverage",
     "a coverage baseline that disagrees with the measurement",
     [("verify/coverage_baseline.json", '"uncovered_lines": 4074',
       '"uncovered_lines": 4073')]),

    ("fuzz",
     "a property check that can no longer be satisfied",
     [("mbt/fuzz_props/main.mbt",
       "fn all_finite(a : FixedArray[Float]) -> Bool {\n  let mut i = 0",
       "fn all_finite(a : FixedArray[Float]) -> Bool {\n  return false\n"
       "  let mut i = 0")]),

    ("stress",
     "a failing test in the wave the gate repeats",
     [("mbt/tests/wave1/relu_test.mbt", None,
       '///|\ntest "qa battery probe" {\n'
       '  fail("the stress gate must be able to fail")\n}')]),

    ("mutation",
     "the CPU reference loop the gate mutates, moved past its pattern",
     [("mbt/gpu/cuda/linear_accel_stub.c",
       "for (int64_t o = 0; o < m; o++)",
       "for (int64_t o = 0; o < m ; o++)")]),

    ("oracle",
     "the oracle's own tolerance, set to zero",
     [("verify/gen_model_oracle.py", "    tol = 50.0 * noise",
       "    tol = 0.0 * noise")]),

    ("provenance",
     "the reference checkout the audit compares against",
     [("_build/tarnet_ref/tarnet/models/tarnet.py",
       "kersize: tuple[int, int, int] = (3, 5, 9),",
       "kersize: tuple[int, int, int] = (3, 5, 7),")]),

    # The release gate exists for exactly one bug: a step that `cd`s into the
    # directory its own job already put it in. The v0.166.1 form was `cd mbt`
    # on its own line. The one-line form -- `run: cd mbt && ...` -- is the same
    # mistake, and a check that only matched the first spelling walked straight
    # past the second. Both are faults here, one per spelling, so a regression
    # in either one is named rather than hidden behind the other.
    ("release",
     "`cd mbt` on its own line under a job that already runs in mbt",
     [(".github/workflows/publish.yml",
       "          # `find`, not a glob: `ls *_test.mbt` with no match hands the",
       "          cd mbt\n"
       "          # `find`, not a glob: `ls *_test.mbt` with no match hands the")]),

    ("release",
     "`run: cd mbt` on one line under a job that already runs in mbt",
     [(".github/workflows/publish.yml",
       "        run: moon test ./tests/wave1 --target native",
       "        run: cd mbt && moon test ./tests/wave1 --target native")]),

    # The formatter's distance from canonical form is a RECORD, and the gate
    # fails when it moves in either direction. A file that is currently
    # canonical becomes non-canonical the moment the formatter's one
    # remaining disagreement with it appears -- here, a missing trailing
    # newline, which `moon fmt` puts back. Nothing about the code changes:
    # the file is byte-for-byte the same program.
    ("fmt",
     "the trailing newline the formatter wants back",
     [("mbt/tests/wave1/units_test.mbt",
       "  assert_eq(@core.nM, 0.001F)\n}\n",
       "  assert_eq(@core.nM, 0.001F)\n}")]),

    # The package graph is three levels deeper than the recorded shape, which
    # is the condition the `deps` gate exists to report. The chain it builds
    # is core -> activ -> nn -> optim -> rl -> attention -> generative ->
    # models -> meta -> infra: ten packages, where the tree records seven.
    # The added edges are acyclic, which matters: a cycle would be a build
    # error, and the gate would never get to measure it.
    ("deps",
     "a dependency chain three levels deeper than the recorded shape",
     [("mbt/generative/moon.pkg",
       '  "riantr/snn_mbt/rl",\n}\n',
       '  "riantr/snn_mbt/rl",\n  "riantr/snn_mbt/attention",\n}\n'),
      ("mbt/models/moon.pkg",
       '  "riantr/snn_mbt/rl",\n}\n',
       '  "riantr/snn_mbt/rl",\n  "riantr/snn_mbt/generative",\n}\n'),
      ("mbt/meta/moon.pkg",
       '  "riantr/snn_mbt/optim",\n}\n',
       '  "riantr/snn_mbt/optim",\n  "riantr/snn_mbt/models",\n}\n'),
      ("mbt/infra/moon.pkg",
       '  "riantr/snn_mbt/models",\n}\n',
       '  "riantr/snn_mbt/models",\n  "riantr/snn_mbt/meta",\n}\n')]),
]


def run_gate(gate):
    """Run one gate and return (exit code, output)."""
    p = subprocess.run([sys.executable, QA, gate], cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return p.returncode, p.stdout.decode("utf-8", errors="replace")


def verdict(out):
    """The gate's own one-line verdict, for the report.

    The harness prints the gate's name, then erases the line and rewrites it
    with the verdict, so the output carries a carriage return between the two.
    Splitting on "\r" as well as "\n" is what gets the real line out rather
    than the progress marker.
    """
    for line in out.replace("\r", "\n").splitlines():
        line = line.strip()
        if line.startswith("[") and "]" in line and "..." not in line:
            return line[:150]
    lines = [ln.strip() for ln in out.replace("\r", "\n").splitlines() if ln.strip()]
    return lines[-1][:150] if lines else "(no output)"


def safe(text):
    """A line that the console can print, whatever the console's codec is."""
    return text.encode("ascii", "replace").decode("ascii")


def apply_edit(data, old, new):
    """Apply one edit to a file's bytes, matching the file's own line endings.

    Returns the new bytes, or None when the anchor is not there. The anchor
    is written with "\n" and translated, because half this tree is CRLF and
    half is LF and a battery that only works on one of them is a battery
    that silently stops canarying.
    """
    text = data.decode("utf-8")
    nl = "\r\n" if "\r\n" in text else "\n"
    if old is None:
        if not text.endswith(nl):
            text += nl
        text += new.replace("\n", nl) + nl
        return text.encode("utf-8")
    o = old.replace("\n", nl)
    if o not in text:
        return None
    return text.replace(o, new.replace("\n", nl), 1).encode("utf-8")


def main():
    wanted = set(sys.argv[1:])
    faults = [f for f in FAULTS if not wanted or f[0] in wanted]
    unknown = wanted - {f[0] for f in FAULTS}
    if unknown:
        print("no fault is defined for gate(s): %s" % ", ".join(sorted(unknown)),
              file=sys.stderr)
        return 2

    print("=== gate battery: %d gate(s) ===\n" % len(faults))
    results = []
    for gate, what, edits in faults:
        started = time.time()
        # Snapshot every file the fault touches, and refuse to start if one
        # of them is not there -- a fault that cannot be applied proves
        # nothing, and silently skipping it would be the battery's own
        # version of the defect it exists to catch.
        snapshots = {}
        missing = []
        for path, _old, _new in edits:
            full = os.path.join(ROOT, path.replace("/", os.sep))
            if not os.path.isfile(full):
                missing.append(path)
                continue
            with open(full, "rb") as fh:
                snapshots[path] = fh.read()
        if missing:
            print("[%s] SKIP  %s (missing: %s)"
                  % (gate, what, ", ".join(missing)))
            results.append((gate, "skip", "missing " + ", ".join(missing)))
            continue

        applied = True
        try:
            for path, old, new in edits:
                full = os.path.join(ROOT, path.replace("/", os.sep))
                out = apply_edit(snapshots[path], old, new)
                if out is None:
                    applied = False
                    break
                with open(full, "wb") as fh:
                    fh.write(out)
            if not applied:
                print("[%s] SKIP  the anchor was not found, so the fault could "
                      "not be applied" % gate)
                results.append((gate, "skip", "anchor not found"))
                continue

            red_rc, red_out = run_gate(gate)
        finally:
            # Restore byte-exactly, even if the gate raised or the run was
            # interrupted. A battery that leaves a fault behind is a battery
            # that poisons the tree it is supposed to be measuring.
            for path, data in snapshots.items():
                full = os.path.join(ROOT, path.replace("/", os.sep))
                with open(full, "wb") as fh:
                    fh.write(data)

        green_rc, green_out = run_gate(gate)
        ok = red_rc != 0 and green_rc == 0
        detail = safe("%s -> %s" % (verdict(red_out), verdict(green_out)))
        print("[%s] %s  %s  (%.0fs)"
              % (gate, "PROVED" if ok else "NOT PROVED", detail,
                 time.time() - started))
        results.append((gate, "proved" if ok else "not proved", detail))

    proved = [r for r in results if r[1] == "proved"]
    skipped = [r for r in results if r[1] == "skip"]
    broken = [r for r in results if r[1] == "not proved"]
    print("\n=== %d proved, %d not proved, %d skipped ==="
          % (len(proved), len(broken), len(skipped)))
    if broken:
        print("\nthese gates could not be made to fail:")
        for gate, _s, detail in broken:
            print("  %s: %s" % (gate, detail))
    if skipped:
        print("\nskipped:")
        for gate, _s, detail in skipped:
            print("  %s: %s" % (gate, detail))
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
