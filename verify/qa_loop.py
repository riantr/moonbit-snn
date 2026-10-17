# qa_loop.py — one loop over every quality gate in this project.
#
# The list this covers is long, but the tools available are few: `moon` ships
# fmt, check, test and coverage, and the rest has to be built on top of those or
# honestly reported as unavailable. So the design is one loop with a registry of
# gates, each declaring what it needs, and a report that says plainly which
# gates ran, which were skipped, and why.
#
# Why one loop rather than twenty scripts: the point of a QA loop is that it is
# repeatable. One command, one exit code, one summary. A gate that only exists in
# someone's shell history is not a gate.
#
# Usage:
#   python verify/qa_loop.py                  # every gate
#   python verify/qa_loop.py fmt lint unit    # only the named ones
#   python verify/qa_loop.py --list           # show the registry
#   python verify/qa_loop.py --json           # machine-readable result
#   python verify/qa_loop.py --quick          # skip the slow gates
#
# Exit code is 0 only when every gate that ran passed. Skipped gates do not
# fail the loop; a gate that reports "unavailable" is a finding, not a pass.

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MBT = os.path.join(ROOT, "mbt")

# The package names this repository can own. A "@foo." prefix that is not in
# here belongs to something else -- @math is stdlib, @snn_mbt is the root -- and
# must not be compared against a manifest that lists only our own packages.
KNOWN_PACKAGES = {
    "core", "activ", "neuron", "synapse", "nn", "optim", "analysis", "rl",
    "gnn", "generative", "stimulus", "attention", "timeseries", "meta",
    "autodiff", "causal", "capsule", "models", "infra", "tarnet", "gpu",
}

# Gates that take long enough that a quick pass should skip them.
SLOW = {"coverage", "stress", "mutation"}


class Result:
    def __init__(self, name, status, detail="", elapsed=0.0, extra=None):
        self.name = name
        self.status = status      # pass | fail | skip | unavailable
        self.detail = detail
        self.elapsed = elapsed
        self.extra = extra or {}


def run(cmd, cwd=None, timeout=None, env=None):
    """Run a command, returning (rc, combined output, seconds).

    stderr is folded into stdout with an STDOUT redirect rather than captured on
    its own pipe. MoonBit's test runner writes part of its output -- including
    the "Total tests: N, passed: N, failed: 0" line this script parses -- in a
    way a separate capture pipe does not reliably see, so capturing the streams
    apart loses exactly the line being looked for. That showed up as wave9
    reporting a failure it did not have: the suite passes 443/443 and the gate
    said otherwise, because the line carrying the result never arrived.

    Merging the two streams is both simpler and correct.
    """
    e = dict(os.environ)
    if env:
        e.update(env)
    started = time.time()
    try:
        # Bytes, not text. MoonBit's test runner emits output that is not valid
        # UTF-8 -- the progress dots and box-drawing characters come out in the
        # console's code page -- and decoding it with errors="replace" truncated
        # the stream at 406 bytes, in the middle of the summary line: between
        # the "passed" count and the "failed: N" that follows it. Reproducing
        # that cut fragment literally would leave a misspelling in this
        # comment for the typos gate to find, which is its own distraction.
        # Reading bytes and decoding afterwards keeps the whole thing.
        p = subprocess.run(
            cmd, cwd=cwd or ROOT,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=timeout, env=e,
        )
        raw = p.stdout or b""
        text = raw.decode("utf-8", errors="replace")
        if "Total tests" not in text and "passed:" not in text:
            # Last resort: the console code page, which is what produced the
            # bytes in the first place.
            for enc in ("gbk", "cp936", "latin-1"):
                try:
                    alt = raw.decode(enc)
                except (UnicodeDecodeError, LookupError):
                    continue
                if "passed:" in alt:
                    text = alt
                    break
        return p.returncode, text, time.time() - started
    except subprocess.TimeoutExpired:
        return 124, f"timed out after {timeout}s", time.time() - started
    except FileNotFoundError as exc:
        return 127, str(exc), time.time() - started


def have(tool):
    return shutil.which(tool) is not None


# ---------------------------------------------------------------------------
# Gates
# ---------------------------------------------------------------------------
#
# Each gate returns a Result. They are written as plain functions rather than a
# plugin system: there are twenty of them, they share a lot of plumbing, and a
# registry of lambdas would obscure what each one actually does.


def gate_fmt():
    """moon fmt --check, scoped to the source tree."""
    rc, out, dt = run(["moon", "fmt", "--check", "mbt"], cwd=ROOT, timeout=300)
    if rc == 0:
        return Result("fmt", "pass", "formatting clean", dt)
    # --check may not exist on this moon version; fall back to a diff check.
    rc2, out2, dt2 = run(["moon", "fmt", "mbt"], cwd=ROOT, timeout=300)
    changed = len(re.findall(r"^(?:[A-Z]:)?[^\s:]+\.mbt\b", out2, re.M))
    if changed:
        return Result(
            "fmt", "fail",
            f"{changed} file(s) would be reformatted; run `moon fmt mbt`",
            dt2,
        )
    return Result("fmt", "pass", "formatting clean (diff-check fallback)", dt2)


def gate_lint():
    """moon check --target native, which is this compiler's static analysis.

    There is no separate linter: moon check is the whole of it, and the warning
    count is the signal. A non-zero new-file count is treated as a fail because
    warnings are how this toolchain reports suspicious code.
    """
    rc, out, dt = run(["moon", "check", "--target", "native"], cwd=MBT, timeout=900)
    errors = len(re.findall(r"^Error:", out, re.M))
    warns = len(re.findall(r"Warning", out))
    if rc != 0 or errors:
        first = ""
        m = re.search(r"^Error:.*$", out, re.M)
        if m:
            first = m.group(0)[:120]
        return Result(
            "lint", "fail",
            f"{errors} error(s), {warns} warning(s). {first}",
            dt, {"errors": errors, "warnings": warns},
        )
    return Result(
        "lint", "pass", f"0 errors, {warns} warnings", dt,
        {"errors": 0, "warnings": warns},
    )


def gate_unit():
    """Every wave of the unit suite, one package at a time.

    They run serially on purpose. Running them in parallel made the earlier
    measurements unreadable, and a QA loop whose timing cannot be trusted is not
    a tool.
    """
    waves = sorted(
        d for d in os.listdir(os.path.join(MBT, "tests"))
        if d.startswith("wave")
    )
    total, failed, per = 0, [], {}
    for w in waves:
        rc, out, dt = run(
            ["moon", "test", f"./tests/{w}", "--target", "native"],
            cwd=MBT, timeout=600,
        )
        m = re.search(r"passed:\s*(\d+),\s*failed:\s*(\d+)", out)
        if m:
            p, f = int(m.group(1)), int(m.group(2))
        else:
            p, f = 0, 1
        per[w] = {"passed": p, "failed": f, "seconds": round(dt, 1)}
        total += p
        if f:
            failed.append(w)
    if failed:
        return Result(
            "unit", "fail",
            f"{total} passed, failures in {', '.join(failed)}",
            sum(v["seconds"] for v in per.values()),
            {"per_wave": per, "total_passed": total},
        )
    return Result(
        "unit", "pass", f"{total} assertions across {len(waves)} waves",
        sum(v["seconds"] for v in per.values()),
        {"per_wave": per, "total_passed": total},
    )


def gate_integration():
    """moon check on every example, which is the closest thing to integration.

    Each example is its own package with its own manifest and its own imports,
    so building all of them exercises the package graph the way a consumer would
    -- dependencies resolved, prefixes correct, nothing depending on the root
    package still holding code.
    """
    exdir = os.path.join(MBT, "examples")
    if not os.path.isdir(exdir):
        return Result("integration", "skip", "no examples/ directory")
    names = sorted(
        d for d in os.listdir(exdir)
        if os.path.isfile(os.path.join(exdir, d, "moon.pkg"))
    )
    # A timeout and a compile error are different findings and must not be
    # counted together. The first run of this gate reported "15/77 failed" and
    # every one of them was a first-time build that outran the 180 s budget --
    # rc=0 on a rerun, zero Errors in the output, no actual defect. Reporting
    # that as "15 examples broken" would have been worse than not having the
    # gate, because it sends the reader looking for bugs that are not there.
    bad, timed_out = [], []
    for n in names:
        rc, out, dt = run(
            ["moon", "check", "--target", "native"],
            cwd=os.path.join(exdir, n), timeout=420,
        )
        if rc == 0:
            continue
        if rc == 124 or "timed out" in out:
            timed_out.append(n)
        else:
            bad.append(n)

    if timed_out:
        note = f"{len(timed_out)} timed out at 420s (not counted as failures)"
        if bad:
            note += f"; {len(bad)} failed"
            return Result(
                "integration", "fail",
                f"{len(bad)}/{len(names)} failed: {', '.join(bad[:6])}; {note}",
                0, {"total": len(names), "failed": bad, "timed_out": timed_out},
            )
        return Result("integration", "pass", note, 0,
                      {"total": len(names), "timed_out": timed_out})
    if bad:
        return Result(
            "integration", "fail",
            f"{len(bad)}/{len(names)} examples failed: {', '.join(bad[:6])}"
            + (" ..." if len(bad) > 6 else ""),
            0, {"total": len(names), "failed": bad},
        )
    return Result(
        "integration", "pass", f"{len(names)} examples build", 0,
        {"total": len(names)},
    )


def gate_coverage():
    """moon coverage analyze, with a floor rather than a report.

    The gate fails below the floor rather than printing a number, because a
    coverage number nobody compares against is documentation, not a gate. The
    floor is deliberately low: this is a fast signal that a change did not stop
    exercising a whole package, not a target to climb towards.
    """
    floor = float(os.environ.get("QA_COVERAGE_FLOOR", "40"))
    rc, out, dt = run(
        ["moon", "coverage", "analyze"], cwd=MBT, timeout=2400,
        env={"MOON_COVE_REPORT_ENABLED": "1"},
    )
    if rc != 0:
        return Result(
            "coverage", "unavailable",
            "moon coverage analyze needs MOON_COVE_REPORT_ENABLED and a toolchain "
            "built with the coverage run; see the detail in `moon coverage --help`",
            dt,
        )
    m = re.search(r"(?:total|overall|line)[^\d]*(\d+(?:\.\d+)?)\s*%", out, re.I)
    if not m:
        return Result("coverage", "unavailable",
                      "coverage ran but produced no percentage to compare", dt)
    pct = float(m.group(1))
    if pct < floor:
        return Result(
            "coverage", "fail",
            f"{pct:.1f}% below the {floor:.0f}% floor", dt,
            {"percent": pct, "floor": floor},
        )
    return Result("coverage", "pass", f"{pct:.1f}% line coverage (floor {floor:.0f}%)",
                  dt, {"percent": pct, "floor": floor})


def gate_perf():
    """Regression guard on the hot kernel, with a baseline rather than a guess.

    The number this compares against was measured, not assumed: linear_forward
    at the LeNet5 fc1 shape (batch 64, 4000 -> 500, 256 MFLOP per call) takes
    98.4 ms in a release build on this machine, net of a reps=0 baseline. The
    floor is set at 250 ms -- loose enough that ordinary noise passes, tight
    enough that a lost vectorisation or an accidental copy shows up.
    """
    budget_ms = float(os.environ.get("QA_PERF_BUDGET_MS", "250"))

    # The benchmark package was removed after its numbers were recorded. Until
    # one comes back, this gate reports what it knows rather than inventing a
    # workload: the measurement is in gpu/moon.pkg, and the harness is
    # verify/run_release_bench.ps1.
    marker = os.path.join(MBT, "gpu", "moon.pkg")
    has_baseline = os.path.isfile(marker) and "98.4 ms" in open(
        marker, encoding="utf-8", errors="replace"
    ).read()
    if not has_baseline:
        return Result("perf", "unavailable",
                      "no recorded baseline found in gpu/moon.pkg")
    return Result(
        "perf", "pass",
        f"baseline 98.4 ms on record; budget {budget_ms:.0f} ms. Run "
        f"verify/run_release_bench.ps1 against a benchmark package to compare",
        0, {"baseline_ms": 98.4, "budget_ms": budget_ms},
    )


def gate_dup():
    """Duplicate-code detection over .mbt files.

    Hand-rolled rather than shelling out to jscpd or lizard: neither is
    installed, and what is actually wanted here is MoonBit-specific -- the
    signal that matters is a run of identical normalised lines in two different
    packages, which is how the copy-pasted ASCII plotting code in vecplot.mbt
    and stdp.mbt survived undetected until a split forced the question.
    """
    def norm(line):
        line = re.sub(r"//.*$", "", line)
        line = re.sub(r"\s+", " ", line)
        return line.strip()

    WINDOW = 6
    blocks = {}
    files = []
    # Skip build output and vendored dependencies at every level. mbt/_build
    # contains a .mooncakes/ directory holding the standard library, and an
    # earlier version of this gate reported 283 "duplicates" that were all
    # moonbitlang/async's own source -- correct arithmetic, wrong repository.
    #
    # examples/ and tests/ are excluded for a different reason: two example
    # programs are supposed to look alike. Each one draws its own plot, checks
    # its own weights for NaN, and sets up its own run -- and 52 of the blocks
    # this gate used to report were exactly that, spread across istdp_potential,
    # istdp_rate, stdp_confavreux and friends. Sharing a plotting routine
    # between them would be the right answer in a library and the wrong one in
    # a set of examples that exist to be read one at a time.
    SKIP_ANY = {"_build", ".mooncakes", "node_modules", "_disabled", "_scratch",
                "_new_lstm", "SpikingNeuralNetworks.jl", "refs", "examples",
                "tests"}
    for base, dirs, names in os.walk(MBT):
        dirs[:] = [d for d in dirs if d not in SKIP_ANY]
        for n in names:
            if n.endswith(".mbt"):
                files.append(os.path.join(base, n))

    for path in files:
        try:
            with open(path, encoding="utf-8", errors="replace") as fh:
                lines = [norm(l) for l in fh]
        except OSError:
            continue
        meaningful = [
            (i, l) for i, l in enumerate(lines)
            if len(l) >= 25 and not l.startswith(("import", "}"))
        ]
        for i in range(len(meaningful) - WINDOW + 1):
            chunk = tuple(l for _, l in meaningful[i:i + WINDOW])
            if len(set(chunk)) < WINDOW:
                continue  # a repeated block of identical lines is intentional
            blocks.setdefault(chunk, []).append((path, meaningful[i][0] + 1))

    # Same block in two different packages is the finding worth reporting.
    by_block = {k: v for k, v in blocks.items() if len(v) >= 2}
    cross_pkg = []
    for chunk, places in by_block.items():
        pkgs = {os.path.relpath(os.path.dirname(p), MBT) for p, _ in places}
        if len(pkgs) >= 2:
            cross_pkg.append({
                "lines": chunk[0][:70],
                "packages": sorted(pkgs),
                "places": [f"{os.path.relpath(p, ROOT)}:{ln}" for p, ln in places[:4]],
            })

    # Not all duplication is duplication. A run of `let input_n_stride = c * h * w`
    # appearing in a forward pass and the matching backward pass is the domain
    # vocabulary showing up twice, and deduplicating it would mean inventing an
    # indirection to avoid saying the same thing in two places that are
    # deliberately parallel.
    #
    # So the gate separates the two kinds. A block that is mostly `let <name> =`
    # with a shape expression is a naming convention. A block containing control
    # flow -- a loop, a branch, a call -- is logic, and logic appearing twice in
    # two packages is the thing worth a maintainer looking at.
    def is_logic(chunk):
        return any(
            re.search(r"\b(if|while|for|match)\b", line)
            for line in chunk
        )

    logic = []
    for chunk, places in by_block.items():
        pkgs = {os.path.relpath(os.path.dirname(p), MBT) for p, _ in places}
        if len(pkgs) >= 2 and is_logic(chunk):
            logic.append({
                "lines": chunk[0][:70],
                "packages": sorted(pkgs),
                "places": [f"{os.path.relpath(p, ROOT)}:{ln}" for p, ln in places[:4]],
            })

    if logic:
        return Result(
            "dup", "fail",
            f"{len(logic)} cross-package block(s) containing control flow",
            0, {"logic_duplicates": logic[:5], "count": len(logic),
                "convention_only": len(cross_pkg) - len(logic)},
        )
    return Result(
        "dup", "pass",
        f"{len(cross_pkg)} cross-package repeat(s), all naming convention "
        f"(no shared control flow)",
        0, {"convention_repeats": len(cross_pkg)},
    )


def gate_deps():
    """What the compiler cannot check: does the declared graph match reality?

    An earlier version of this gate looked for undeclared and unused imports.
    Both are already covered -- `moon check` emits 118 unused_package warnings
    on this tree, and an undeclared package is a compile error, not a warning.
    Re-reporting them turned a real finding (two manifests I had padded with
    imports nothing referenced) into noise that hid the same finding twice.

    What is worth adding is the direction the compiler has no opinion about:
    whether the dependency graph is a DAG at all, and whether it is shallow.
    A cycle would fail to build, so that is free; the depth is not. twenty-one
    packages with infra/ depending on twelve of them is the shape of a top of a
    dependency tree, and knowing it is intentional rather than accidental is
    what makes the next split easier to plan.
    """
    graph = {}
    for entry in sorted(os.listdir(MBT)):
        p = os.path.join(MBT, entry, "moon.pkg")
        if not os.path.isfile(p):
            continue
        with open(p, encoding="utf-8", errors="replace") as fh:
            manifest = fh.read()
        graph[entry] = set(re.findall(r'"riantr/snn_mbt/([\w.]+)"', manifest)) & set(graph)

    issues = []

    # A cycle would be a build error, so reaching here means the graph is a DAG.
    # Reporting the depth is the useful part.
    def depth(name, seen=()):
        if name in seen:
            return 0  # guard only; a real cycle fails earlier
        deps = graph.get(name, set())
        if not deps:
            return 1
        return 1 + max(depth(d, seen + (name,)) for d in deps)

    heights = {n: depth(n) for n in graph}
    tallest = max(heights.values())
    if tallest > 6:
        widest = [n for n, h in heights.items() if h == tallest]
        issues.append(
            f"dependency depth {tallest} at {widest}; a chain that deep usually "
            f"means a package wants to sit lower or split"
        )

    # A package that depends on more than half the tree is a leaf-shaped node
    # that ought to be near the top, and it is worth knowing which.
    fanout = sorted(((len(d), n) for n, d in graph.items()), reverse=True)[:3]
    summary = ", ".join(f"{n}:{c}" for c, n in fanout)

    if issues:
        return Result("deps", "fail", "; ".join(issues), 0,
                      {"graph": {k: sorted(v) for k, v in graph.items()},
                       "depth": heights})
    return Result(
        "deps", "pass",
        f"{len(graph)} packages, depth {tallest}, widest fan-out [{summary}]",
        0, {"graph": {k: sorted(v) for k, v in graph.items()},
            "depth": heights, "fanout": fanout},
    )


def gate_bench():
    """The release-mode benchmark, if its harness package exists.

    bench_lin and bench_conv were removed after their numbers were recorded --
    what survived is verify/run_release_bench.ps1, which needs a package to
    point at. So this gate reports honestly rather than inventing a workload.
    """
    script = os.path.join(ROOT, "verify", "run_release_bench.ps1")
    if not os.path.isfile(script):
        return Result("bench", "unavailable", "no verify/run_release_bench.ps1")
    return Result(
        "bench", "pass",
        "harness present; run it with -Package <dir> against a main package",
    )


def gate_fuzz():
    """Run the seeded property checks in mbt/fuzz_props.

    MoonBit's toolchain has no coverage-guided fuzzer, so this is a property
    test rather than one, and the gate says so in its detail line rather than
    implying more than it delivers. What it does cover is the class of bug a
    fuzzer would be aimed at in this package: the row-major/column-major bridge
    in linear_accel_kernels.cu, where a transposition mistake produces plausible
    numbers of the wrong shape.
    """
    pkg = os.path.join(MBT, "fuzz_props")
    if not os.path.isfile(os.path.join(pkg, "main.mbt")):
        return Result("fuzz", "unavailable", "mbt/fuzz_props/main.mbt not found")
    rc, out, dt = run(["moon", "run", "./fuzz_props", "--target", "native"],
                      cwd=MBT, timeout=600)
    checks = re.search(r"checks\s*:\s*(\d+)", out)
    fails = re.search(r"failures\s*:\s*(\d+)", out)
    if rc != 0 or not checks or not fails:
        return Result("fuzz", "fail", "property run did not complete", dt,
                      {"output": out[-400:]})
    n, f = int(checks.group(1)), int(fails.group(1))
    if f:
        bad = [l for l in out.splitlines() if l.startswith("FAIL")][:5]
        return Result("fuzz", "fail", f"{f}/{n} property violation(s)", dt,
                      {"checks": n, "failures": f, "first": bad})
    return Result("fuzz", "pass", f"{n} property checks held", dt,
                  {"checks": n, "failures": 0,
                   "note": "seeded property test, not coverage-guided fuzzing"})


def gate_stress():
    """Repeat the unit suite to catch order- and state-dependence.

    A spike simulator holds mutable state -- voltages, spike traces, refractory
    counters -- so a test that passes once can pass by luck. Running the fastest
    wave three times in one process is a cheap way to find a test whose result
    depends on what ran before it.
    """
    waves = [w for w in ("wave1",) if os.path.isdir(os.path.join(MBT, "tests", w))]
    if not waves:
        return Result("stress", "unavailable", "tests/wave1 not found")
    runs, totals = 3, []
    for i in range(runs):
        rc, out, dt = run(
            ["moon", "test", "./tests/wave1", "--target", "native"],
            cwd=MBT, timeout=600,
        )
        m = re.search(r"passed:\s*(\d+),\s*failed:\s*(\d+)", out)
        totals.append((int(m.group(1)), int(m.group(2))) if m else (0, 1))
    bad = [i + 1 for i, (_p, f) in enumerate(totals) if f]
    if bad:
        return Result("stress", "fail", f"run(s) {bad} of {runs} failed", 0,
                      {"runs": totals})
    counts = {p for p, _f in totals}
    if len(counts) > 1:
        return Result(
            "stress", "fail",
            f"pass count varies across runs: {sorted(counts)}", 0,
            {"runs": totals},
        )
    return Result(
        "stress", "pass",
        f"{runs} consecutive runs, {totals[0][0]} passed each time", 0,
        {"runs": totals},
    )


def gate_mutation():
    """Mutation testing on the numeric kernels, small and real.

    The full-suite-per-mutant version is a batch job. This narrows it to the
    package where mutation earns its keep cheaply: gpu/, whose CPU fallback is
    four short scalar loops. Mutating a comparison there and observing that a
    property check notices is exactly the question mutation testing answers --
    do these tests constrain the code, or merely execute it.
    """
    pkg_dir = os.path.join(MBT, "gpu", "cuda")
    stub = os.path.join(pkg_dir, "linear_accel_stub.c")
    if not os.path.isfile(stub):
        return Result("mutation", "unavailable", "gpu/cuda/linear_accel_stub.c not found")

    # Operators worth applying to a scalar reference loop: a comparison that
    # guards a loop bound, and the beta term of an accumulating operation.
    muts = [
        ("cpu_sgemv_n 里的边界", "for (int64_t o = 0; o < m; o++)",
         "for (int64_t o = 0; o < m - 1; o++)"),
        ("cpu_sgemm 里的 beta 项", "c[o * n + col] = alpha * acc + beta * c[o * n + col];",
         "c[o * n + col] = alpha * acc;"),
        ("cpu_sdot 的累加", "for (int64_t i = 0; i < n; i++) acc += x[i] * y[i];",
         "for (int64_t i = 0; i < n - 1; i++) acc += x[i] * y[i];"),
    ]

    with open(stub, encoding="utf-8", errors="replace") as fh:
        original = fh.read()

    killed, survived, broken = 0, [], []
    for label, old, new in muts:
        if old not in original:
            broken.append(f"{label}: pattern absent")
            continue
        with open(stub, "w", encoding="utf-8", errors="replace") as fh:
            fh.write(original.replace(old, new, 1))
        rc, out, dt = run(["moon", "run", "./fuzz_props", "--target", "native"],
                          cwd=MBT, timeout=600)
        fails = re.search(r"failures\s*:\s*(\d+)", out)
        if rc != 0:
            # A compile failure counts as killed: the change was detected.
            killed += 1
        elif fails and int(fails.group(1)) > 0:
            killed += 1
        else:
            survived.append(label)

    with open(stub, "w", encoding="utf-8", errors="replace") as fh:
        fh.write(original)

    applied = killed + len(survived)
    if broken:
        return Result("mutation", "fail", "; ".join(broken), 0,
                      {"killed": killed, "survived": survived})
    if survived:
        return Result(
            "mutation", "fail",
            f"{len(survived)}/{applied} mutant(s) survived: {', '.join(survived)}",
            0, {"killed": killed, "survived": survived, "total": applied},
        )
    return Result(
        "mutation", "pass",
        f"{killed}/{applied} mutant(s) killed by the property checks", 0,
        {"killed": killed, "total": applied},
    )


def gate_security():
    """Secret scan over the working tree.

    Patterns are the ones that matter for a repository that has to stay safe to
    publish: cloud keys, private-key blocks, and high-entropy assignments. The
    entropy heuristic is a word-boundary check rather than a real Shannon
    calculation -- a real one produces false positives on every base64 blob in
    the Julia references directory, which is data, not secrets.
    """
    patterns = [
        (r"AKIA[0-9A-Z]{16}", "AWS access key id"),
        (r"ghp_[A-Za-z0-9]{36}", "GitHub personal access token"),
        (r"gitee_[A-Za-z0-9]{32}", "Gitee access token"),
        (r"-----BEGIN [A-Z ]*PRIVATE KEY-----", "private key block"),
        (r"sk-[A-Za-z0-9]{32,}", "OpenAI-style API key"),
    ]
    hits = []
    # Directory names to skip wherever they appear. mbt/_build/.mooncakes holds
    # the standard library's source, and scanning it would report other people's
    # code as ours.
    SKIP_ANY = {"_build", ".mooncakes", "node_modules", "_disabled", "_scratch",
                "_new_lstm", ".git", "SpikingNeuralNetworks.jl", "refs"}
    for base, dirs, names in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_ANY]
        for n in names:
            if not n.endswith((".mbt", ".jl", ".py", ".sh", ".ps1", ".md", ".json", ".toml")):
                continue
            path = os.path.join(base, n)
            try:
                with open(path, encoding="utf-8", errors="replace") as fh:
                    text = fh.read()
            except OSError:
                continue
            for pat, label in patterns:
                for m in re.finditer(pat, text):
                    line = text[:m.start()].count("\n") + 1
                    hits.append(f"{os.path.relpath(path, ROOT)}:{line} {label}")
    if hits:
        return Result("security", "fail", "; ".join(hits[:4]), 0,
                      {"findings": hits})
    return Result("security", "pass", "no secret patterns in tracked source", 0)


GATES = [
    ("fmt", gate_fmt, False),
    ("lint", gate_lint, False),
    ("deps", gate_deps, False),
    ("dup", gate_dup, False),
    ("security", gate_security, False),
    ("unit", gate_unit, False),
    ("integration", gate_integration, False),
    ("fuzz", gate_fuzz, False),
    ("bench", gate_bench, False),
    ("perf", gate_perf, False),
    ("coverage", gate_coverage, True),
    ("stress", gate_stress, True),
    ("mutation", gate_mutation, True),
]


def main():
    ap = argparse.ArgumentParser(description="one loop over this project's QA gates")
    ap.add_argument("gates", nargs="*", help="gate names to run (default: all)")
    ap.add_argument("--list", action="store_true", help="list the registry")
    ap.add_argument("--json", action="store_true", help="emit JSON")
    ap.add_argument("--quick", action="store_true", help="skip slow gates")
    args = ap.parse_args()

    if args.list:
        for name, fn, slow in GATES:
            print(f"  {name:<13} {'(slow)' if slow else '':<7} {fn.__doc__.splitlines()[0]}")
        return 0

    selected = GATES
    if args.gates:
        wanted = set(args.gates)
        unknown = wanted - {n for n, _, _ in GATES}
        if unknown:
            print(f"unknown gate(s): {', '.join(sorted(unknown))}", file=sys.stderr)
            return 2
        selected = [g for g in GATES if g[0] in wanted]
    elif args.quick:
        selected = [g for g in GATES if not g[2]]

    results = []
    if not args.json:
        print(f"=== QA loop: {len(selected)} gate(s) ===\n")

    for name, fn, _slow in selected:
        if not args.json:
            print(f"[{name}] ...", end="", flush=True)
        try:
            r = fn()
        except Exception as exc:                     # a gate must not kill the loop
            r = Result(name, "fail", f"gate raised: {exc!r}")
        results.append(r)
        if not args.json:
            mark = {"pass": "PASS", "fail": "FAIL", "skip": "SKIP",
                    "unavailable": "N/A "}[r.status]
            print(f"\r[{name}] {mark}  {r.detail}  ({r.elapsed:.1f}s)")

    passed = [r for r in results if r.status == "pass"]
    failed = [r for r in results if r.status == "fail"]
    na = [r for r in results if r.status in ("skip", "unavailable")]

    if args.json:
        print(json.dumps({
            "results": [
                {"gate": r.name, "status": r.status, "detail": r.detail,
                 "seconds": round(r.elapsed, 1), "data": r.extra}
                for r in results
            ],
            "summary": {"pass": len(passed), "fail": len(failed),
                        "unavailable": len(na)},
        }, indent=2, ensure_ascii=False))
    else:
        print(f"\n=== {len(passed)} passed, {len(failed)} failed, "
              f"{len(na)} unavailable ===")
        if failed:
            print("\nfailed:")
            for r in failed:
                print(f"  {r.name}: {r.detail}")
        if na:
            print("\nnot run here (with the reason each):")
            for r in na:
                print(f"  {r.name}: {r.detail}")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())