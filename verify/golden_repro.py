"""Can every committed oracle golden be reproduced from the generator that ships with it?

WHY THIS EXISTS. Six golden files are checked in, and five generators are
checked in beside them, and NOTHING connected the two. A golden is the
expected answer, so a stale one -- or a hand-edited one -- is not a stale
test, it is a wrong answer that keeps passing. That is the failure this
gate exists to make impossible.

THE BUG THIS FOUND. `gen_block_oracle.py` and `gen_se_oracle.py` could not
run at all. Their `parse()` demanded a `TNORACLE bx` and a `TNORACLE bd`
line in the export, and NO file in this repository has ever emitted one --
not the block test, not the export test, not any commit in the history
(`git log --all -S 'TNORACLE bx'` returns nothing). So both generators
exited with "missing bx in the export" before computing a single number,
and the goldens they were supposed to produce could not be regenerated
at all. The fixture never needed exporting: it is an integer hash, and
both sides build it with integer arithmetic and one division.

WHAT "REPRODUCIBLE" MEANS HERE, and why it is not just "the files are
equal". Four of the six goldens reproduce BYTE FOR BYTE. Two do not, and
not because they are wrong: PyTorch's FFT and its convolutions are not
bit-reproducible across builds, so a golden made by a different PyTorch
differs in the last bits. For those two the rule is instead:

    a golden is reproducible if every element agrees with a fresh
    regeneration to within the SAME bound the MoonBit test applies to
    that array.

That is the tolerance the golden is USED at, so the rule is derived from
the code rather than fitted to whatever comes out. The bounds come from
`block_oracle_test.mbt` (BLOCK_ATOL/BLOCK_RTOL and BLOCK_ATOL_P/BLOCK_RTOL_P)
and `logmel_oracle_test.mbt` (the atol/rtol pairs on L1, L2 and L3).

The block oracle's PARAMETER GRADIENTS are the tight case: they are sums
over 18 time steps and 144 positions, they cancel, and a different PyTorch
build moves them by up to 1.4e-01 against an absolute bound of 2.5e-01.
That is a real finding, not a formality -- see the note in
`block_oracle_test.mbt` about why those gradients get their own looser
bound.

LOCAL BY DESIGN. This needs PyTorch and `moon`, and a fresh export log,
so it is not a gate that can run on a CI runner. It reports `unavailable`
when the tools are missing rather than `pass`, because "did not check" is
a finding and reporting it as a success is the exact failure this
project has paid for before.

Usage:
  python verify/golden_repro.py            # every golden
  python verify/golden_repro.py --json     # machine-readable
  python verify/golden_repro.py --selftest # prove the comparator can fail
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERIFY = os.path.join(ROOT, "verify")
SCRATCH = os.path.join(ROOT, "_scratch", "golden_repro")

NUM = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?F"
ARRAY_RE = re.compile(r"pub let (\w+) : Array\[Float\] = \[(.*?)\n\]", re.S)
CONST_RE = re.compile(r"pub const (\w+) : Int = (-?\d+)")

# (atol, rtol) for a golden's arrays. `default` is used by any array whose
# name is not listed; the block oracle's `_dp_` arrays are the ones that
# need the looser pair, and they are matched by suffix.
BLOCK_TOL = (1.0e-3, 1.0e-3)
BLOCK_TOL_P = (2.5e-1, 0.0)

SPECS = [
    {
        "golden": "mbt/tests/wave1/batch_norm1d_golden.mbt",
        "generators": ["gen_bn1d_golden.py"],
        "argv": [],
        "export": False,
        "mode": "byte",
    },
    {
        "golden": "mbt/tests/wave2/asp_oracle_golden.mbt",
        "generators": ["asp_oracle.py"],
        "argv": [],
        "export": False,
        "mode": "byte",
    },
    {
        "golden": "mbt/tests/wave2/model_oracle_golden.mbt",
        "generators": ["gen_model_oracle.py", "asp_oracle.py",
                       "gen_block_oracle.py", "gen_se_oracle.py"],
        "argv": ["{export}"],
        "export": True,
        "mode": "byte",
    },
    {
        "golden": "mbt/tests/wave2/se_oracle_golden.mbt",
        "generators": ["gen_se_oracle.py", "gen_block_oracle.py"],
        "argv": ["{export}"],
        "export": True,
        "mode": "byte",
    },
    {
        "golden": "mbt/tests/wave2/block_oracle_golden.mbt",
        "generators": ["gen_block_oracle.py"],
        "argv": ["{export}"],
        "export": True,
        "mode": "numeric",
        "tolerances": {
            "default": BLOCK_TOL,
            "_dp_golden": BLOCK_TOL_P,
        },
    },
    {
        "golden": "mbt/tests/wave2/logmel_golden.mbt",
        "generators": ["gen_logmel_golden.py"],
        "argv": ["{export}", "--write"],
        "export": True,
        "mode": "numeric",
        "tolerances": {
            "default": (1.0e-2, 1.0e-4),
            "logmel_fb_golden": (1.0e-5, 1.0e-3),
            "logmel_mel_golden": (1.0e-1, 1.0e-2),
        },
    },
]


def have_torch():
    try:
        import torch  # noqa: F401
        return True
    except Exception:
        return False


def have_moon():
    return shutil.which("moon") is not None


def parse_golden(path):
    """(constants, {array name: [floats]}) from a golden file."""
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    consts = {m.group(1): int(m.group(2)) for m in CONST_RE.finditer(text)}
    arrays = {}
    for m in ARRAY_RE.finditer(text):
        arrays[m.group(1)] = [float(x[:-1]) for x in re.findall(NUM, m.group(2))]
    return consts, arrays


def tolerance_for(name, tolerances):
    for key, tol in tolerances.items():
        if key == "default":
            continue
        if name.endswith(key) or name == key:
            return tol
    return tolerances["default"]


def normalise_bytes(data):
    """Line endings are not content.

    `core.autocrlf` is true in this repository and there is no
    `.gitattributes`, so a golden that arrives from a checkout on Windows
    has CRLF while the generator that writes it emits LF. Comparing raw
    bytes would fail on that alone -- and would fail on Windows and pass
    on Linux, which is the worst kind of gate: one that reports a
    different verdict depending on where it runs.

    Git's own comparison normalizes this away, which is why `git status`
    stays clean on a file that differs on disk.
    """
    return data.replace(b"\r\n", b"\n")


def compare_numeric(committed, fresh, tolerances):
    """(ok, detail). Every element must be inside atol + rtol*|committed|."""
    cconsts, carrays = committed
    fconsts, farrays = fresh

    problems = []
    if cconsts != fconsts:
        only_c = sorted(set(cconsts) - set(fconsts))
        only_f = sorted(set(fconsts) - set(cconsts))
        differ = sorted(k for k in set(cconsts) & set(fconsts)
                         if cconsts[k] != fconsts[k])
        problems.append("constants differ (committed-only: %s, fresh-only: %s, "
                        "changed: %s)" % (only_c, only_f, differ))
    if set(carrays) != set(farrays):
        problems.append("array set differs (committed-only: %s, fresh-only: %s)"
                        % (sorted(set(carrays) - set(farrays)),
                           sorted(set(farrays) - set(carrays))))

    worst = 0.0
    for name in sorted(set(carrays) & set(farrays)):
        a, b = carrays[name], farrays[name]
        if len(a) != len(b):
            problems.append("%s: length %d vs %d" % (name, len(a), len(b)))
            continue
        atol, rtol = tolerance_for(name, tolerances)
        over = 0
        worst_excess = 0.0
        for u, v in zip(a, b):
            d = abs(u - v)
            bound = atol + rtol * abs(u)
            excess = d - bound
            if excess > 0:
                over += 1
                worst_excess = max(worst_excess, excess)
        if over:
            problems.append("%s: %d of %d elements outside atol+rtol*|x| "
                            "(worst excess %.3e)" % (name, over, len(a), worst_excess))
        # how much of the bound the worst element actually used
        used = 0.0
        for u, v in zip(a, b):
            bound = atol + rtol * abs(u)
            if bound > 0:
                used = max(used, abs(u - v) / bound)
        worst = max(worst, used)

    detail = "worst element used %.1f%% of its bound" % (100.0 * worst)
    return (not problems), detail, problems


def run(cmd, cwd=None, timeout=None):
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout,
                           env=dict(os.environ))
        return p.returncode, (p.stdout or b"").decode("utf-8", errors="replace")
    except subprocess.TimeoutExpired:
        return 124, "timed out after %ss" % timeout
    except FileNotFoundError as exc:
        return 127, str(exc)


def make_export_log(scratch):
    """Run the wave2 test package and capture its stdout: that IS the export."""
    log = os.path.join(scratch, "export.log")
    rc, out = run(["moon", "test", "-p", "riantr/snn_mbt/tests/wave2"],
                  cwd=os.path.join(ROOT, "mbt"), timeout=1800)
    with open(log, "w", encoding="utf-8", errors="replace") as fh:
        fh.write(out)
    if "TNORACLE params" not in out:
        return None, "the wave2 run produced no `TNORACLE params` line (exit %s)" % rc
    return log, None


def prepare_scratch():
    if os.path.isdir(SCRATCH):
        shutil.rmtree(SCRATCH, ignore_errors=True)
    os.makedirs(os.path.join(SCRATCH, "verify"), exist_ok=True)
    os.makedirs(os.path.join(SCRATCH, "mbt", "tests", "wave1"), exist_ok=True)
    os.makedirs(os.path.join(SCRATCH, "mbt", "tests", "wave2"), exist_ok=True)
    return SCRATCH


def check_golden(spec, export_log, results):
    golden = os.path.join(ROOT, spec["golden"])
    rel = spec["golden"]
    if not os.path.isfile(golden):
        results.append({"golden": rel, "status": "fail",
                        "detail": "the committed golden is missing"})
        return

    for gen in spec["generators"]:
        src = os.path.join(VERIFY, gen)
        if not os.path.isfile(src):
            results.append({"golden": rel, "status": "fail",
                            "detail": "%s is missing" % gen})
            return
        shutil.copy2(src, os.path.join(SCRATCH, "verify", gen))

    argv = [a.replace("{export}", export_log or "") for a in spec["argv"]]
    rc, out = run([sys.executable, os.path.join(SCRATCH, "verify",
                                                spec["generators"][0])] + argv,
                  cwd=SCRATCH, timeout=1800)
    if rc != 0:
        tail = " | ".join([ln for ln in out.splitlines() if ln.strip()][-3:])
        results.append({"golden": rel, "status": "fail",
                        "detail": "the generator exited %s: %s" % (rc, tail[:300])})
        return

    fresh_path = os.path.join(SCRATCH, *rel.split("/"))
    if not os.path.isfile(fresh_path):
        results.append({"golden": rel, "status": "fail",
                        "detail": "the generator wrote no %s" % rel})
        return

    if spec["mode"] == "byte":
        with open(golden, "rb") as fh:
            want = fh.read()
        with open(fresh_path, "rb") as fh:
            got = fh.read()
        if normalise_bytes(want) == normalise_bytes(got):
            note = "byte-identical to a fresh regeneration"
            if want != got:
                note += " (after line-ending normalisation: the file on disk " \
                        "uses CRLF, the generator writes LF)"
            results.append({"golden": rel, "status": "pass", "detail": note})
        else:
            results.append({"golden": rel, "status": "fail",
                            "detail": "regenerates to a DIFFERENT file "
                                      "(%d committed bytes vs %d regenerated)"
                                      % (len(want), len(got))})
        return

    ok, detail, problems = compare_numeric(parse_golden(golden),
                                           parse_golden(fresh_path),
                                           spec["tolerances"])
    if ok:
        results.append({"golden": rel, "status": "pass", "detail": detail})
    else:
        results.append({"golden": rel, "status": "fail",
                        "detail": "; ".join(problems)[:400], "problems": problems})


def selftest():
    """Prove the comparator can fail: perturb one element and it must notice."""
    # (constants, {array name: [values]}) -- the shape compare_numeric takes.
    base = ({}, {"x": [2.0], "y": [-3.0]})
    fresh = ({}, {"x": [2.0], "y": [-3.0]})
    tol = {"default": (1.0e-3, 1.0e-3)}
    ok, _detail, _p = compare_numeric(base, fresh, tol)
    if not ok:
        print("SELFTEST FAIL: identical inputs were rejected")
        return 1
    # 1e-2 apart on a value of 2.0, against a bound of 1e-3 + 1e-3*2.0
    fresh[1]["x"] = [2.01]
    ok, _detail, problems = compare_numeric(base, fresh, tol)
    if ok:
        print("SELFTEST FAIL: a 1e-2 perturbation was accepted")
        return 1
    if not problems:
        print("SELFTEST FAIL: a perturbation was rejected without a reason")
        return 1
    # a missing array must be caught
    del fresh[1]["y"]
    ok, _detail, _p = compare_numeric(base, fresh, tol)
    if ok:
        print("SELFTEST FAIL: a missing array was accepted")
        return 1
    # a changed constant must be caught
    ok, _detail, _p = compare_numeric(({"K": 3}, {"x": [1.0]}),
                                      ({"K": 4}, {"x": [1.0]}), tol)
    if ok:
        print("SELFTEST FAIL: a changed constant was accepted")
        return 1
    print("SELFTEST PASS: identical inputs accepted; a 1e-2 perturbation, a "
          "missing array and a changed constant all rejected")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if not have_torch():
        print("PyTorch is not installed, so no golden can be regenerated.")
        print("This is an environment limitation, not a verdict: the goldens "
              "were NOT checked.")
        return 2
    if not have_moon():
        print("`moon` is not on PATH, so the export log cannot be produced.")
        print("This is an environment limitation, not a verdict: the goldens "
              "were NOT checked.")
        return 2

    prepare_scratch()
    export_log = None
    export_error = None
    if any(s["export"] for s in SPECS):
        export_log, export_error = make_export_log(SCRATCH)
        if export_log is None:
            print("Could not produce the export log: %s" % export_error)
            print("This is an environment limitation, not a verdict: the "
                  "goldens were NOT checked.")
            return 2

    results = []
    for spec in SPECS:
        check_golden(spec, export_log, results)

    passed = sum(1 for r in results if r["status"] == "pass")
    failed = sum(1 for r in results if r["status"] == "fail")
    if args.json:
        print(json.dumps({"results": results,
                          "summary": {"pass": passed, "fail": failed}},
                         indent=2))
    else:
        for r in results:
            print("[%s] %-8s %s" % (r["status"].upper().ljust(8),
                                    r["golden"], r["detail"]))
        print()
        print("%d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
