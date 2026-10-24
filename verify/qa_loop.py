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
import ast
import filecmp
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

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
SLOW = {"coverage", "stress", "mutation", "golden", "bench", "perf"}


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


def _canonical_offenders():
    """The files the formatter would rewrite, measured exactly.

    `moon fmt --check` cannot answer this question. It formats the project,
    diffs it against the source, and STOPS after ten differences: on a clean
    tree it formatted 22 files and reported 10, and the ten it reported
    ROTATED between runs on the same tree. It is a good way to find one file
    to look at and a bad way to count -- and a repository note that said
    "ten of them carry pre-existing formatting diffs" was written on the
    strength of that sample. The real number is 621 files, of 640.

    So the measurement is done on a copy: copy the module, format the copy,
    compare it with the original. That is exact, it is the same set every
    time, and the real tree is never touched.

    Returns (offenders, None) or (None, reason).
    """
    tmp = tempfile.mkdtemp(prefix="moonbit_snn_fmt_")
    try:
        dst = os.path.join(tmp, "mbt")
        shutil.copytree(MBT, dst, ignore=shutil.ignore_patterns(
            "_build", ".mooncakes", "_scratch", ".git"))
        p = subprocess.run(["moon", "fmt"], cwd=dst, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=900)
        if p.returncode != 0:
            tail = (p.stdout or b"").decode("utf-8", errors="replace")
            return None, "`moon fmt` on the copy exited %d: %s" % (
                p.returncode, tail.strip().splitlines()[-1][:160]
                if tail.strip() else "(no output)")
        offenders = []
        for base, dirs, names in os.walk(dst):
            dirs[:] = [d for d in dirs
                       if d not in ("_build", ".mooncakes", "_scratch")]
            for n in names:
                if not n.endswith(".mbt"):
                    continue
                new = os.path.join(base, n)
                rel = os.path.relpath(new, dst).replace("\\", "/")
                old = os.path.join(MBT, rel.replace("/", os.sep))
                if not os.path.isfile(old):
                    continue
                if not filecmp.cmp(old, new, shallow=False):
                    offenders.append("mbt/" + rel)
        return sorted(offenders), None
    except (OSError, subprocess.SubprocessError) as exc:
        return None, "could not measure the formatter's verdict: %r" % (exc,)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def gate_fmt():
    """How far is the tree from the formatter's canonical form, and is it moving?

    The answer is a long way: 621 of the source files are not in the
    current formatter's canonical form, because the tree predates the
    spelling the toolchain now prefers (`Int` for `Int32`, `x?` for `x~`)
    and the formatter also wants a trailing newline on most files. That is
    a fact about the tree, not a defect in it -- the code compiles and
    1925 tests pass -- and it is not worth a 621-file rewrite inside a QA
    pass, especially since the rewrite would reflow the checked-in oracle
    goldens and break the byte identity the `golden` gate exists to hold.

    So this gate does not demand canonical form. It MEASURES the distance
    and compares it with a recorded measurement, and it fails when the
    distance changes in either direction: a file that becomes non-canonical
    is drift, and a file that becomes canonical means the record is stale.
    Either way the baseline in verify/fmt_baseline.json has to be updated
    in the same commit, which is what makes it a record of the measurement
    rather than a target to be fitted to.

    The previous version of this gate ran `moon fmt --check` from the
    repository root, where there is no moon.mod and no moon.work, so every
    invocation died with "not in a Moon project", the fallback could not
    run either, and the gate reported "formatting clean" on every run for
    as long as it existed. It was a gate that could not fail, and the
    reason nothing noticed is that nothing ever asked it to.
    """
    started = time.time()
    offenders, err = _canonical_offenders()
    dt = time.time() - started
    if offenders is None:
        return Result("fmt", "unavailable", err, dt)
    base = load_fmt_baseline()
    if base is None:
        return Result(
            "fmt", "fail",
            "verify/fmt_baseline.json is missing or malformed, so the "
            "formatter's verdict cannot be compared against anything. "
            "Restore it rather than letting the measurement drift unchecked.",
            dt,
        )
    digest = hashlib.sha256("\n".join(offenders).encode("utf-8")).hexdigest()
    count = len(offenders)
    if count != base["non_canonical_files"] or digest != base["set_sha256"]:
        grew = count > base["non_canonical_files"]
        return Result(
            "fmt", "fail",
            "the formatter would rewrite %d file(s) and the recorded "
            "measurement says %d. %s Update verify/fmt_baseline.json in the "
            "same commit: it is a record of the measurement, not a target to "
            "be fitted to."
            % (count, base["non_canonical_files"],
               "The tree moved FURTHER from canonical form."
               if grew else
               "The tree moved CLOSER to canonical form, so the record is "
               "stale."),
            dt,
            {"non_canonical_files": count,
             "expected": base["non_canonical_files"],
             "set_sha256": digest, "expected_sha256": base["set_sha256"],
             "sample": offenders[:10]},
        )
    return Result(
        "fmt", "pass",
        "%d of the source files are not in the formatter's canonical form, "
        "which is the recorded state" % count,
        dt, {"non_canonical_files": count, "set_sha256": digest},
    )


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
    # The count is pinned, not merely reported. Four places quote it - the
    # two READMEs, the tag messages, and this gate's own summary - and
    # nothing made them agree: the repository carried 1884 in one README
    # and 1899 in the other at the same time, while the suite ran 1925.
    # A count that is only asserted rots; a count that is pinned cannot.
    base = load_test_baseline()
    if base is None:
        return Result(
            "unit", "fail",
            "verify/test_baseline.json is missing or malformed, so the number of "
            "tests the suite runs cannot be compared against anything. Restore "
            "it rather than letting the count drift unchecked.",
            sum(v["seconds"] for v in per.values()),
            {"per_wave": per, "total_passed": total},
        )
    if total != base["total_tests"]:
        return Result(
            "unit", "fail",
            f"the suite runs {total} tests, the baseline says "
            f"{base['total_tests']}. A test was added or removed: update "
            f"verify/test_baseline.json and every count quoted in README.md "
            f"and mbt/README.md in the same commit, or the documentation and "
            f"the suite will disagree again.",
            sum(v["seconds"] for v in per.values()),
            {"per_wave": per, "total_passed": total,
             "baseline_tests": base["total_tests"]},
        )
    return Result(
        "unit", "pass", f"{total} assertions across {len(waves)} waves",
        sum(v["seconds"] for v in per.values()),
        {"per_wave": per, "total_passed": total},
    )


def _check_claims(claims, placeholder, value):
    """Which of the enumerated documentation claims no longer hold.

    A claim is a (file, regex) pair. The regex carries a placeholder for the
    pinned number, so the claim is checked against the CURRENT number rather
    than against whatever the prose happened to say when it was written.

    EVERY sentence of the claim's shape has to carry that number, not merely
    one of them. The weaker rule -- "the number appears somewhere in the
    file" -- let a wrong count hide behind a right one: the file said 1926 in
    one place and 1925 in another, and the search found the 1925 and called
    the claim satisfied. A stale number next to a fresh one is still stale.

    Returns a list of (file, pattern) for the claims that did not match.
    """
    missing = []
    for claim in claims:
        rel = claim.get("file", "")
        pattern = claim.get("pattern", "")
        if not rel or not pattern:
            missing.append((rel or "?", "(a claim with no file or pattern)"))
            continue
        path = os.path.join(ROOT, rel)
        try:
            with open(path, encoding="utf-8") as fh:
                text = fh.read()
        except OSError as exc:
            missing.append((rel, f"unreadable: {exc}"))
            continue
        probe = pattern.replace(placeholder, r"(?P<__pinned>\d+)")
        if probe == pattern:
            # No placeholder in this claim, so there is no number to compare:
            # the claim is that the shape appears at all.
            if not re.search(pattern, text):
                missing.append((rel, pattern))
            continue
        try:
            hits = list(re.finditer(probe, text))
        except re.error as exc:
            missing.append((rel, "unreadable pattern: %s" % exc))
            continue
        if not hits:
            missing.append((rel, pattern))
            continue
        for m in hits:
            got = m.group("__pinned")
            if got != str(value):
                missing.append((rel, "%s (says %s, expected %s)"
                                % (pattern, got, value)))
                break
    return missing


def gate_docs():
    """The documented counts, checked against the pinned baselines.

    A number in prose is a snapshot with no expiry date. The test count is
    pinned by the `unit` gate; this gate checks that the READMEs still quote
    the pinned number, so that the chain suite -> baseline -> documentation
    cannot come apart.

    The gate count is pinned the same way, and it is pinned here because it
    had already drifted twice. The READMEs said "Fifteen gates" and
    "thirteen quality gates" while the registry held nineteen, and nothing
    noticed: a gate that counts the gates is the only thing that can.

    The shape of the tree is pinned here too, in verify/tree_baseline.json.
    The documentation said 732 source files because the count came from a
    directory walk, and a walk sees the vendored dependency cache; the two
    READMEs also disagreed with each other, because each carried its own
    count. The tree is now measured from what git actually tracks.

    And the per-row test counts in the package README's feature table are
    checked against the test files they name, which is the check that would
    have caught the four wrong counts and the one count naming a file that
    does not exist.

    The claims are enumerated in the baseline files rather than discovered by
    scanning, because a scan cannot tell a current claim from a historical
    one. The historical ones now live in mbt/CHANGELOG.md, which no claim
    reads: a changelog records the counts as they were.
    """
    base = load_test_baseline()
    if base is None:
        return Result(
            "docs", "fail",
            "verify/test_baseline.json is missing or malformed, so the counts "
            "quoted in the documentation cannot be checked against anything.",
        )
    n = base["total_tests"]
    claims = base.get("claims") or []
    missing = _check_claims(claims, "{n}", n)
    if missing:
        first = missing[0]
        return Result(
            "docs", "fail",
            "%d of %d documented test counts no longer match the pinned "
            "number (%d). The suite runs %d tests; the documentation now says "
            "something else. Update the quoted count, or update "
            "verify/test_baseline.json if the suite itself changed. First "
            "offender: %s -- %s"
            % (len(missing), len(claims), n, n, first[0], first[1]),
            0.0,
            {"missing": missing, "expected": n},
        )

    # The gate count and the CI gate count: the registry is the authority, the
    # baseline says what the registry SHOULD hold, and the documentation has to
    # quote it.
    gbase = load_gate_baseline()
    if gbase is None:
        return Result(
            "docs", "fail",
            "verify/gate_baseline.json is missing or malformed, so the number "
            "of gates cannot be compared against anything. Restore it rather "
            "than letting the count drift unchecked.",
        )

    # (label, baseline document, its file, key, placeholder, claims key,
    #  the live value -- None when the pinned number IS the authority)
    specs = [
        ("gate", gbase, "gate_baseline.json", "gate_count", "{g}",
         "claims", len(GATES)),
        ("CI gate", gbase, "gate_baseline.json", "ci_gate_count", "{c}",
         "ci_claims", len(CI_GATES)),
    ]
    checked = []
    for label, doc, fname, key, placeholder, claim_key, live in specs:
        pinned = doc.get(key)
        if not isinstance(pinned, int):
            return Result(
                "docs", "fail",
                "verify/%s does not pin a %s, so the number the documentation "
                "quotes cannot be checked against anything." % (fname, label),
            )
        actual = live if live is not None else pinned
        if actual != pinned:
            return Result(
                "docs", "fail",
                "the registry holds %d %s(s) and verify/%s pins %d. A gate was "
                "added or removed: update the baseline and every count quoted "
                "in README.md and mbt/README.md in the same commit, or the "
                "documentation and the harness will disagree again."
                % (actual, label, fname, pinned),
                0.0,
                {key: actual, "expected": pinned},
            )
        cclaims = doc.get(claim_key) or []
        cmissing = _check_claims(cclaims, placeholder, actual)
        if cmissing:
            first = cmissing[0]
            return Result(
                "docs", "fail",
                "%d of %d documented %s counts no longer match the pinned "
                "number (%d). The registry holds %d; the documentation now "
                "says something else. Update the quoted count, or update "
                "verify/%s if the harness changed. First offender: %s -- %s"
                % (len(cmissing), len(cclaims), label, actual, actual, fname,
                   first[0], first[1]),
                0.0,
                {"missing": cmissing, "expected": actual},
            )
        checked.append((label, len(cclaims), actual))

    # The shape of the tree. These are the numbers that drifted, and they
    # drifted for a reason worth recording: the documentation said 732 source
    # files because the count came from a directory walk, and a walk sees the
    # 400 vendored `.mbt` files that `moon update` leaves in `mbt/.mooncakes/`.
    # That is the dependency cache, not this package's source. The two READMEs
    # also disagreed with each other -- 681 against 732 source files, 23
    # against 299 -- because each document carried its own count and nothing
    # made them agree.
    #
    # So the tree is measured here from what git actually tracks, compared with
    # verify/tree_baseline.json, and then the READMEs are checked for still
    # quoting it. The chain is tree -> baseline -> documentation, the same
    # chain the test count has, and it closes in the same commit.
    tbase = load_tree_baseline()
    if tbase is None:
        return Result(
            "docs", "fail",
            "verify/tree_baseline.json is missing or malformed, so the counts "
            "describing the shape of the tree cannot be compared against "
            "anything. Restore it rather than letting them drift unchecked.",
        )
    measured = _measure_tree()
    if measured is None:
        return Result(
            "docs", "fail",
            "could not measure the shape of the tree: `git ls-files` did not "
            "succeed, so the documented file counts have nothing to be "
            "compared against. A gate that cannot check anything is not a "
            "gate.",
        )
    facts = tbase.get("facts") or {}
    if not facts:
        return Result(
            "docs", "fail",
            "verify/tree_baseline.json records no facts, so there is nothing "
            "to compare the tree against.",
        )
    drift = []
    for name in sorted(facts):
        pinned = facts[name]
        live = measured.get(name)
        if live != pinned:
            drift.append((name, live, pinned))
    if drift:
        first = drift[0]
        return Result(
            "docs", "fail",
            "%d of %d recorded tree counts moved: %s is %s now and the "
            "baseline says %s. The tree changed shape: update "
            "verify/tree_baseline.json and every count quoted in README.md "
            "and mbt/README.md in the same commit, or the documentation and "
            "the tree will disagree again."
            % (len(drift), len(facts), first[0],
               "not measurable" if first[1] is None else first[1],
               first[2]),
            0.0,
            {"drift": drift, "measured": measured, "expected": facts},
        )

    # A fact this gate does not measure but another gate owns. The formatter's
    # distance from canonical form is measured by the `fmt` gate and pinned in
    # verify/fmt_baseline.json. This gate does not re-measure it -- that would
    # run `moon fmt` a second time for the same number -- but it does check
    # that the READMEs quote the number the `fmt` gate owns, so the two
    # baselines cannot drift apart silently.
    pinned = dict(facts)
    for name in sorted(tbase.get("borrowed") or {}):
        spec = (tbase.get("borrowed") or {}).get(name) or {}
        path = os.path.join(ROOT, "verify", spec.get("file", ""))
        try:
            with open(path, encoding="utf-8") as fh:
                other = json.load(fh)
            value = other.get(spec.get("key", ""))
        except (OSError, ValueError):
            value = None
        if not isinstance(value, int):
            return Result(
                "docs", "fail",
                "the borrowed fact %r is supposed to come from verify/%s, and "
                "it could not be read as an integer. The gate that owns that "
                "number has to be able to publish it." % (name, spec.get("file")),
                0.0, {"borrowed": name},
            )
        pinned[name] = value

    # And the documentation has to quote the pinned numbers.
    tree_claims = tbase.get("claims") or []
    missing_tree = []
    for name in sorted(pinned):
        for claim in tree_claims:
            if claim.get("fact") != name:
                continue
            missing_tree.extend(
                _check_claims([claim], "{%s}" % name, pinned[name]))
    if missing_tree:
        first = missing_tree[0]
        return Result(
            "docs", "fail",
            "%d documented tree count(s) no longer match the pinned numbers. "
            "The tree measures %s; the documentation now says something else. "
            "Update the quoted count, or update verify/tree_baseline.json if "
            "the tree itself changed. First offender: %s -- %s"
            % (len(missing_tree), ", ".join(
                "%s=%d" % (k, pinned[k]) for k in sorted(pinned)),
               first[0], first[1]),
            0.0,
            {"missing": missing_tree, "expected": pinned},
        )

    # The per-row test counts in the package README's feature table. Each row
    # that states a count states it as "N tests in `<file>`", so the number can
    # be looked up in the file it names and compared. This is the check that
    # would have caught the four counts in that table that were wrong and the
    # one that named a file which does not exist -- all of which sat in a
    # published package page for months because nothing compared them with
    # the suite.
    counts = _test_declaration_counts()
    pkg_readme = os.path.join(MBT, "README.md")
    try:
        with open(pkg_readme, encoding="utf-8") as fh:
            pkg_text = fh.read()
    except OSError:
        pkg_text = ""
    claimed_counts = re.findall(
        r"(\d+) tests? in `([\w./-]+_test\.mbt)`", pkg_text)
    wrong_counts = []
    for claimed_n, f in claimed_counts:
        base = os.path.basename(f)
        actual = counts.get(base)
        if actual is None:
            wrong_counts.append((f, int(claimed_n), None))
        elif actual != int(claimed_n):
            wrong_counts.append((f, int(claimed_n), actual))
    if wrong_counts:
        first = wrong_counts[0]
        return Result(
            "docs", "fail",
            "%d of %d test count(s) in mbt/README.md no longer match the file "
            "they name. %s is claimed to hold %d test(s)%s. A count that "
            "cannot be checked against the suite is a number that rots: fix "
            "the count, or drop it if the file no longer covers that row."
            % (len(wrong_counts), len(claimed_counts), first[0], first[1],
               "" if first[2] is None else
               (" but the file now holds %d" % first[2])),
            0.0,
            {"wrong": wrong_counts, "checked": len(claimed_counts)},
        )

    return Result(
        "docs", "pass",
        "%d documented test count(s) agree with the pinned %d; %s; %d "
        "documented tree count(s) agree with the pinned tree shape; %d "
        "per-row test count(s) agree with the files they name"
        % (len(claims), n,
           "; ".join("%d documented %s counts agree with the pinned %d"
                     % (c, label, v) for label, c, v in checked),
           len(tree_claims), len(claimed_counts)),
        0.0, {"claims": len(claims), "expected": n,
              "checked": [{"label": l, "claims": c, "value": v}
                          for l, c, v in checked],
              "tree_facts": pinned, "tree_claims": len(tree_claims),
              "row_counts": len(claimed_counts)},
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
    started = time.time()
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
    elapsed = time.time() - started

    if timed_out:
        note = f"{len(timed_out)} timed out at 420s (not counted as failures)"
        if bad:
            note += f"; {len(bad)} failed"
            return Result(
                "integration", "fail",
                f"{len(bad)}/{len(names)} failed: {', '.join(bad[:6])}; {note}",
                elapsed, {"total": len(names), "failed": bad,
                          "timed_out": timed_out},
            )
        return Result("integration", "pass", note, elapsed,
                      {"total": len(names), "timed_out": timed_out})
    if bad:
        return Result(
            "integration", "fail",
            f"{len(bad)}/{len(names)} examples failed: {', '.join(bad[:6])}"
            + (" ..." if len(bad) > 6 else ""),
            elapsed, {"total": len(names), "failed": bad},
        )
    return Result(
        "integration", "pass", f"{len(names)} examples build", elapsed,
        {"total": len(names)},
    )


COVERAGE_BASELINE = os.path.join(ROOT, "verify", "coverage_baseline.json")
TEST_BASELINE = os.path.join(ROOT, "verify", "test_baseline.json")
GATE_BASELINE = os.path.join(ROOT, "verify", "gate_baseline.json")
FMT_BASELINE = os.path.join(ROOT, "verify", "fmt_baseline.json")
TREE_BASELINE = os.path.join(ROOT, "verify", "tree_baseline.json")
DEPS_BASELINE = os.path.join(ROOT, "verify", "deps_baseline.json")


def load_baseline(path, key):
    """A recorded baseline, or None if the file is missing or unusable.

    The file is the gate's memory. A gate that cannot find its own baseline
    reports the problem rather than guessing one, because a guessed baseline
    is the same thing as no baseline except that it looks like a measurement.
    """
    try:
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
    except (OSError, ValueError):
        return None
    if not isinstance(doc, dict):
        return None
    if not isinstance(doc.get(key), int):
        return None
    return doc


def load_coverage_baseline():
    """The recorded coverage baseline, or None if it cannot be read."""
    return load_baseline(COVERAGE_BASELINE, "uncovered_lines")


def load_test_baseline():
    """The recorded test-count baseline, or None if it cannot be read."""
    return load_baseline(TEST_BASELINE, "total_tests")


def load_gate_baseline():
    """The recorded gate-count baseline, or None if it cannot be read."""
    return load_baseline(GATE_BASELINE, "gate_count")


def load_deps_baseline():
    """The recorded package-graph baseline, or None if it cannot be read."""
    return load_baseline(DEPS_BASELINE, "dependency_depth")


def load_tree_baseline():
    """The recorded tree-shape baseline, or None if it cannot be read.

    Not `load_baseline`, because this file holds a set of counts rather than
    one: the facts live under "facts" and every one of them has to be an
    integer, or the file is not a baseline this gate can compare against.
    """
    try:
        with open(TREE_BASELINE, encoding="utf-8") as fh:
            doc = json.load(fh)
    except (OSError, ValueError):
        return None
    if not isinstance(doc, dict):
        return None
    facts = doc.get("facts")
    if not isinstance(facts, dict) or not facts:
        return None
    for name, value in facts.items():
        if not isinstance(value, int):
            return None
    return doc


def _tracked_paths():
    """The paths git tracks under mbt/, or None if git cannot answer.

    `git ls-files` and not a directory walk, and the difference is not a
    detail. A walk sees the 400 vendored `.mbt` files that `moon update` leaves
    in `mbt/.mooncakes/` -- the dependency cache, not this package's source --
    plus the four scratch files under the gitignored `mbt/_disabled/` and
    `mbt/_new_lstm/` directories. That is how the documentation came to say the
    tree held 732 source files: it counted the cache.

    Git knows which files the repository owns, so asking git is both the
    correct answer and the reproducible one. It is also available on a CI
    runner, where actions/checkout leaves a working git repository.
    """
    rc, out, _ = run(["git", "-C", ROOT, "ls-files", "--", "mbt"])
    if rc != 0:
        return None
    return [line.strip() for line in out.splitlines() if line.strip()]


def _test_declaration_counts():
    """How many test cases each test file under mbt/tests/ declares.

    The count is the number of `test "..."` declarations in the file, which is
    what `moon test` reports for that file.
    """
    counts = {}
    root = os.path.join(MBT, "tests")
    if not os.path.isdir(root):
        return counts
    for dp, _dn, fn in os.walk(root):
        for name in fn:
            if not name.endswith(".mbt"):
                continue
            try:
                with open(os.path.join(dp, name), encoding="utf-8",
                          errors="replace") as fh:
                    text = fh.read()
            except OSError:
                continue
            counts[name] = len(re.findall(r'^\s*test\s+"', text, re.M))
    return counts


def _measure_tree():
    """The shape of the tree, measured from what git actually tracks.

    Returns a dict of fact -> count, or None if the tree cannot be measured.
    The definitions are the ones recorded in verify/tree_baseline.json, so the
    measurement and the pinned numbers are talking about the same thing.
    """
    paths = _tracked_paths()
    if paths is None:
        return None
    sources = [p for p in paths if p.endswith(".mbt")]
    library = [p for p in sources
               if not p.startswith("mbt/tests/")
               and not p.startswith("mbt/examples/")]
    tests = [p for p in sources if p.startswith("mbt/tests/")]

    packages = [e for e in os.listdir(MBT)
                if os.path.isfile(os.path.join(MBT, e, "moon.pkg"))]
    examples = []
    exroot = os.path.join(MBT, "examples")
    if os.path.isdir(exroot):
        examples = [e for e in os.listdir(exroot)
                    if os.path.isfile(os.path.join(exroot, e, "moon.pkg"))]

    root_imports = 0
    try:
        with open(os.path.join(MBT, "moon.pkg"), encoding="utf-8",
                  errors="replace") as fh:
            manifest = fh.read()
        root_imports = len(set(re.findall(r'"riantr/snn_mbt/([\w.]+)"', manifest)))
    except OSError:
        pass

    return {
        "library_source_files": len(library),
        "packages": len(packages),
        "root_imports": root_imports,
        "test_files": len(tests),
        "example_packages": len(examples),
    }


def load_fmt_baseline():
    """The recorded formatter baseline, or None if it cannot be read."""
    if not os.path.isfile(FMT_BASELINE):
        return None
    try:
        with open(FMT_BASELINE, encoding="utf-8") as fh:
            doc = json.load(fh)
    except (OSError, ValueError):
        return None
    if not isinstance(doc.get("non_canonical_files"), int):
        return None
    if not isinstance(doc.get("set_sha256"), str):
        return None
    return doc


def gate_coverage():
    """Uncovered executable lines, against a measured baseline.

    `moon coverage analyze` used to print a percentage and this gate compared
    it to a floor. The current toolchain prints no percentage at all - only

        Total: 4062 uncovered line(s) in 289 file(s)

    - so there is nothing to compare against a floor, and a gate that parses
    a percentage it can no longer find is a gate that silently reports
    "unavailable" forever. The count is compared instead, against the
    baseline recorded in verify/coverage_baseline.json.

    The baseline is a measurement, not a fitted tolerance: it is the number
    the toolchain printed on the committed tree, recorded next to the commit
    it was measured on and the command that produced it. The default
    tolerance is zero lines, because the count repeats exactly across
    instrumented runs of the same tree - so a rise is a real change in what
    the suite exercises rather than noise. QA_COVERAGE_TOLERANCE widens it
    for a change that deliberately lands untested code.

    The count is the toolchain's own count of uncovered EXECUTABLE lines. It
    is not the number of source lines in the raw coverage dumps, which is a
    different and larger number, and it must not be recomputed from them.
    """
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
    m = re.search(
        r"Total:\s*([\d,]+)\s+uncovered line\(s\)\s+in\s+([\d,]+)\s+file\(s\)", out
    )
    if not m:
        return Result(
            "coverage", "unavailable",
            "moon coverage analyze printed no uncovered-line count to compare; the "
            "toolchain's output format changed and this gate has to follow it",
            dt,
        )
    uncovered = int(m.group(1).replace(",", ""))
    files = int(m.group(2).replace(",", ""))
    base = load_coverage_baseline()
    if base is None:
        return Result(
            "coverage", "fail",
            "verify/coverage_baseline.json is missing or malformed, so there is "
            "nothing to compare the count against. Restore it rather than "
            "letting this gate go quiet.",
            dt,
        )
    try:
        tol = int(os.environ.get("QA_COVERAGE_TOLERANCE", "0"))
    except ValueError:
        tol = 0
    budget = base["uncovered_lines"] + tol
    extra = {
        "uncovered_lines": uncovered,
        "files": files,
        "baseline_lines": base["uncovered_lines"],
        "baseline_files": base.get("files"),
        "tolerance": tol,
    }
    if uncovered > budget:
        return Result(
            "coverage", "fail",
            "%d uncovered line(s) in %d file(s), %d above the baseline of %d "
            "(tolerance %d). Add the missing tests, or if the new code is "
            "deliberately untested, update verify/coverage_baseline.json with "
            "the reason." % (
                uncovered, files, uncovered - base["uncovered_lines"],
                base["uncovered_lines"], tol,
            ),
            dt, extra,
        )
    return Result(
        "coverage", "pass",
        "%d uncovered line(s) in %d file(s) (baseline %d, tolerance %d)" % (
            uncovered, files, base["uncovered_lines"], tol,
        ),
        dt, extra,
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


def gate_shadow():
    """Is any name defined twice at module level in this repository's Python?

    Python lets the later definition win, silently. A function that is
    defined and then defined again never runs: the first copy is dead text
    that still reads as if it were live.

    That is not hypothetical. `verify/qa_loop.py` carried two versions of
    `gate_perf` and two of `gate_bench` at the same time -- the early drafts
    of both. The drafts reported a recorded 98.4 ms baseline against a 250 ms
    budget, while the gates that actually ran measured the kernel and
    compared it with verify/perf_baseline.json. So the dead copies also
    quoted a figure that was stale by 1.3x, in a file whose whole job is
    quoting figures that are not stale.

    The check is AST-based rather than a regex, because a regex cannot tell
    a definition from a mention of one, and this file is full of mentions.
    Only module-level bindings are considered: a local name inside a
    function is a different thing entirely.
    """
    skip_dirs = {".git", "_build", "_scratch", "node_modules", ".mooncakes",
                 "__pycache__", ".venv", "venv",
                 # The reference checkouts are gitignored, so they exist on
                 # some machines and not on others. Scanning them would make
                 # the file count in this gate's own message depend on
                 # whether the developer happens to have the Julia sources
                 # lying around -- and a count that changes with the machine
                 # is a count nobody can compare against anything.
                 "SpikingNeuralNetworks.jl", "SNNModels.jl", "SNNUtils.jl"}
    files = []
    for base, dirs, names in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for name in sorted(names):
            if name.endswith(".py"):
                files.append(os.path.join(base, name))

    problems = []
    for path in files:
        rel = os.path.relpath(path, ROOT)
        try:
            with open(path, encoding="utf-8-sig") as fh:
                tree = ast.parse(fh.read(), filename=path)
        except (OSError, SyntaxError, ValueError) as exc:
            problems.append("%s: cannot be parsed (%s)" % (rel, exc))
            continue

        # Module-level imports, by the name they bind.
        imported = {}
        for node in tree.body:
            if isinstance(node, ast.Import):
                for alias in node.names:
                    imported[alias.asname or alias.name.split(".")[0]] = node.lineno
            elif isinstance(node, ast.ImportFrom):
                for alias in node.names:
                    imported[alias.asname or alias.name] = node.lineno

        defined = {}
        for node in tree.body:
            if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                                     ast.ClassDef)):
                continue
            name = node.name
            if name in defined:
                problems.append(
                    "%s:%d: `%s` is defined again at line %d, so the definition "
                    "at line %d never runs" % (rel, node.lineno, name,
                                               defined[name], defined[name])
                )
                continue
            defined[name] = node.lineno
            if name in imported:
                problems.append(
                    "%s:%d: `%s` shadows the import on line %d"
                    % (rel, node.lineno, name, imported[name])
                )

    if problems:
        return Result("shadow", "fail",
                      "%d shadowed module-level definition(s): %s"
                      % (len(problems), "; ".join(problems[:4])),
                      0, {"problems": problems, "files_scanned": len(files)})
    return Result("shadow", "pass",
                  "%d Python file(s), no name defined twice at module level"
                  % len(files),
                  0, {"files_scanned": len(files)})


def gate_deps():
    """What the compiler cannot check: does the declared graph match reality?

    An earlier version of this gate looked for undeclared and unused imports.
    Both are already covered -- `moon check` emits 118 unused_package warnings
    on this tree, and an undeclared package is a compile error, not a warning.
    Re-reporting them turned a real finding (two manifests I had padded with
    imports nothing referenced) into noise that hid the same finding twice.

    What is worth adding is the direction the compiler has no opinion about:
    whether the dependency graph is a DAG at all, and whether it is shallow.
    A cycle would fail to build, so that is free; the depth is not. Twenty-four
    packages with infra/ depending on eleven of them is the shape of a top of a
    dependency tree, and knowing it is intentional rather than accidental is
    what makes the next split easier to plan.

    The depth is recorded rather than thresholded by a round number, because
    the shape of the tree is a fact about the tree and not about this gate.
    The recorded value and the tolerance live in verify/deps_baseline.json,
    and the gate fails when the depth grows past them.
    """
    manifests = {}
    for entry in sorted(os.listdir(MBT)):
        p = os.path.join(MBT, entry, "moon.pkg")
        if not os.path.isfile(p):
            continue
        with open(p, encoding="utf-8", errors="replace") as fh:
            manifests[entry] = fh.read()

    # The edges are resolved against the WHOLE tree, and not against the
    # packages that happen to have been read already.
    #
    # The first version intersected each manifest with the graph built SO FAR,
    # and the loop walks the directory in sorted order -- so an edge to a
    # LATER-sorting package was dropped on the floor. Twenty-eight edges went
    # missing that way, including eight of infra/'s eleven. The tree was
    # measured as 4 deep when it is 7 deep, and the wide fan-out was reported
    # as `tarnet` at 5 when it is `infra` at 11.
    #
    # That is the failure this repository exists to catch: a gate that
    # under-reports the thing it exists to report cannot fail. Its only
    # failure condition, a depth above 6, was unreachable on the real graph
    # because the measurement was wrong, and the number both READMEs quoted
    # was wrong with it.
    #
    # A nested package (`gpu/cuda`) is not a top-level package, so it is not
    # an edge either; and a package that names itself is not a dependency.
    graph = {}
    for entry, manifest in manifests.items():
        deps = re.findall(r'"riantr/snn_mbt/([\w./-]+)"', manifest)
        graph[entry] = {d for d in deps if d in manifests and d != entry}

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
    widest_at_top = sorted(n for n, h in heights.items() if h == tallest)

    base = load_deps_baseline()
    if base is None:
        return Result(
            "deps", "fail",
            "verify/deps_baseline.json is missing or malformed, so the shape of "
            "the package graph cannot be compared against anything. Restore it "
            "rather than letting the depth drift unchecked.",
            0, {"depth": heights},
        )
    recorded = base["dependency_depth"]
    tolerance = base.get("tolerance", 0)
    if tallest > recorded + tolerance:
        issues.append(
            "dependency depth %d exceeds the recorded %d (+%d tolerance) at %s; "
            "a chain that deep usually means a package wants to sit lower or "
            "split"
            % (tallest, recorded, tolerance, ", ".join(widest_at_top))
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
        f"{len(graph)} packages, depth {tallest} (recorded {recorded}), "
        f"widest fan-out [{summary}]",
        0, {"graph": {k: sorted(v) for k, v in graph.items()},
            "depth": heights, "fanout": fanout},
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
    started = time.time()
    for i in range(runs):
        rc, out, dt = run(
            ["moon", "test", "./tests/wave1", "--target", "native"],
            cwd=MBT, timeout=600,
        )
        m = re.search(r"passed:\s*(\d+),\s*failed:\s*(\d+)", out)
        totals.append((int(m.group(1)), int(m.group(2))) if m else (0, 1))
    elapsed = time.time() - started
    bad = [i + 1 for i, (_p, f) in enumerate(totals) if f]
    if bad:
        return Result("stress", "fail", f"run(s) {bad} of {runs} failed", elapsed,
                      {"runs": totals})
    counts = {p for p, _f in totals}
    if len(counts) > 1:
        return Result(
            "stress", "fail",
            f"pass count varies across runs: {sorted(counts)}", elapsed,
            {"runs": totals},
        )
    return Result(
        "stress", "pass",
        f"{runs} consecutive runs, {totals[0][0]} passed each time", elapsed,
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
    started = time.time()
    try:
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
    finally:
        # The source file is restored even if the run raised -- and a run CAN
        # raise: `run` propagates a timeout. Without this, a timeout leaves a
        # MUTANT in the tree, and the next commit ships it. A harness that
        # edits the thing it is testing has to put it back on the way out,
        # not only on the way out of the happy path.
        with open(stub, "w", encoding="utf-8", errors="replace") as fh:
            fh.write(original)
    elapsed = time.time() - started

    applied = killed + len(survived)
    if broken:
        return Result("mutation", "fail", "; ".join(broken), elapsed,
                      {"killed": killed, "survived": survived})
    if survived:
        return Result(
            "mutation", "fail",
            f"{len(survived)}/{applied} mutant(s) survived: {', '.join(survived)}",
            elapsed, {"killed": killed, "survived": survived, "total": applied},
        )
    return Result(
        "mutation", "pass",
        f"{killed}/{applied} mutant(s) killed by the property checks", elapsed,
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
    return Result("security", "pass",
                  "no secret patterns in the source tree (every file under "
                  "the repo except the skipped directories)", 0)


def gate_oracle():
    """The PyTorch oracle's self-test: does its own backward match its own forward?

    This is the gate that had to exist first. The oracle is what the whole-model
    test compares against, and it shipped having never been run: its selftest
    reported `absmax(x) / absmax(x)`, which is 1.0 by construction, and it wired
    the SE after the ASP head where the channel counts do not even match. Six of
    its defects were its own, and four fix-diagnose-revert cycles blamed the port
    for faults that lived in the reference.

    Runs in well under a second and needs no MoonBit, no goldens and no export,
    which is the point: a reference nobody can validate alone is a reference
    nobody validates.
    """
    script = os.path.join(ROOT, "verify", "gen_model_oracle.py")
    if not os.path.isfile(script):
        return Result("oracle", "unavailable", "verify/gen_model_oracle.py not found", 0)
    rc, text, dt = run([sys.executable, script, "--selftest"], timeout=600)
    m = re.search(r"worst relative error ([\d.e+-]+), tolerance ([\d.e+-]+)", text)
    if rc != 0:
        tail = [ln for ln in text.splitlines() if "Error" in ln or "assert" in ln]
        return Result("oracle", "fail",
                      "; ".join(tail[:2]) or text.strip().splitlines()[-1:], dt)
    if not m:
        return Result("oracle", "fail", "selftest produced no verdict line", dt,
                      {"output": text[-400:]})
    return Result("oracle", "pass",
                  "worst relative error %s against tolerance %s"
                  % (m.group(1), m.group(2)), dt)


def gate_provenance():
    """Is the oracle still a transcription of the reference, or of a memory of it?

    The oracle's entire claim to independence is one sentence in its header: it
    is transcribed from `_build/tarnet_ref/tarnet/models/tarnet.py` rather than
    from this port. That sentence is what makes its verdict worth anything, and
    it was an assertion. If the reference drifts, nothing notices.

    `_build/` is gitignored, so this is a LOCAL gate: the reference is a checkout,
    not a tracked file. Missing reference reports `unavailable` rather than
    `pass`, because "did not check" is a finding and reporting it as a success
    is the exact failure this project has already paid for once.
    """
    script = os.path.join(ROOT, "verify", "audit_transcription.py")
    if not os.path.isfile(script):
        return Result("provenance", "unavailable",
                      "verify/audit_transcription.py not found", 0)
    rc, text, dt = run([sys.executable, script], timeout=300)
    if rc == 2:
        return Result("provenance", "unavailable",
                      "reference not checked out under _build/", dt)
    m = re.search(r"transcription audit: (\d+)/(\d+) structural facts hold", text)
    if rc != 0 or not m:
        failed = [ln.strip() for ln in text.splitlines() if "[FAIL]" in ln]
        return Result("provenance", "fail",
                      "; ".join(failed[:2]) or "audit produced no verdict", dt)
    return Result("provenance", "pass",
                  "%s of %s structural facts hold against the reference"
                  % (m.group(1), m.group(2)), dt)


def _workflow_jobs(lines):
    """Split a workflow's lines into (job name, lines) blocks.

    A workflow's jobs are two-space-indented keys under `jobs:`. Splitting on
    those is what makes a `working-directory` belong to the job that declared
    it: a `cd` in one job cannot contradict another job's working directory.
    Treating the whole file as one job invents contradictions that do not
    exist -- and a gate that cries wolf is a gate that gets switched off.
    """
    jobs = []
    current = None
    in_jobs = False
    for n, line in lines:
        if re.match(r"^jobs:\s*$", line):
            in_jobs = True
            continue
        if not in_jobs:
            continue
        m = re.match(r"^  ([A-Za-z0-9_.-]+):\s*$", line)
        if m:
            current = (m.group(1), [])
            jobs.append(current)
            continue
        if current is not None:
            current[1].append((n, line))
    return jobs


def _job_default_workdir(job_lines):
    """The job's `defaults.run.working-directory`, if it declares one.

    Only the block BEFORE `steps:` is consulted. A `working-directory:`
    inside a step is that step's override, not the job's, and treating it as
    the job's default would make every other step's `cd` look wrong.
    """
    in_defaults = False
    for _n, line in job_lines:
        if re.match(r"^\s*steps:\s*$", line):
            break
        if re.match(r"^\s*defaults:\s*$", line):
            in_defaults = True
            continue
        if in_defaults:
            m = re.match(r"\s*working-directory:\s*(\S+)\s*$", line)
            if m:
                return m.group(1).strip("/")
    return None


def _workflow_steps(job_lines):
    """Split a job's lines into (label, lines) blocks, one per step.

    A step is a list item under `steps:`. Splitting on those is what lets a
    step's own `working-directory:` be applied to that step alone: a `cd` in
    one step cannot contradict another step's directory, and resolving the
    whole job as a single directory invents contradictions that do not
    exist -- which is how a gate ends up crying wolf.
    """
    steps = []
    current = None
    in_steps = False
    for n, line in job_lines:
        if re.match(r"^\s*steps:\s*$", line):
            in_steps = True
            continue
        if not in_steps:
            continue
        m = re.match(r"^\s*-\s+name:\s*(.*)$", line)
        if m:
            current = (m.group(1).strip() or "(unnamed step)", [])
            steps.append(current)
            current[1].append((n, line))
            continue
        if re.match(r"^\s*-\s+", line):
            current = ("(unnamed step)", [])
            steps.append(current)
            current[1].append((n, line))
            continue
        if current is not None:
            current[1].append((n, line))
    return steps


def gate_release():
    """Is every workflow consistent with itself, in the two ways they have broken?

    Both of these cost a release, and neither was visible locally.

    1. A cold runner's registry index is empty, so `moon check` failed with
       "Failed to resolve registry dependency" before a single file was
       checked. The fix was `moon update` -- and it has to come BEFORE the
       first `moon check`, not merely exist somewhere in the file.

    2. The job declares `working-directory: mbt`, so every step already runs
       in the package root. The guard step then ran `cd mbt`, asked for
       `mbt/mbt`, and died on the path. The runner is `bash -e`, so that one
       failure took the thirteen steps behind it -- including the publish --
       with it, and the release reported what looked like a content failure
       instead of a path error.

    EVERY workflow file is audited, not only the release one. Both failures
    above are properties of a workflow's own body, so a second workflow can
    commit them just as easily -- and a gate that only knows about one file
    cannot catch the same mistake the second time it is made.

    The check is narrow on purpose: it knows the two contradictions that have
    bitten, and it names the offending line. "3 problems" is not something
    anybody can act on.

    Comments are stripped before anything is parsed. They are where the
    history of these failures is written down, and a gate that reads its own
    documentation as its subject finds the documentation.
    """
    workflows = os.path.join(ROOT, ".github", "workflows")
    if not os.path.isdir(workflows):
        return Result("release", "fail", "%s not found" % workflows, 0)
    names = sorted(n for n in os.listdir(workflows)
                   if n.endswith((".yml", ".yaml")))
    if not names:
        return Result("release", "fail", "no workflow file in %s" % workflows, 0)

    problems = []
    audited = []
    workdirs_seen = set()

    for name in names:
        path = os.path.join(workflows, name)
        try:
            with open(path, encoding="utf-8") as fh:
                raw = fh.read()
        except OSError as exc:
            problems.append("%s: unreadable (%s)" % (name, exc))
            continue

        lines = []
        for n, line in enumerate(raw.splitlines(), 1):
            if line.strip().startswith("#"):
                continue
            lines.append((n, line))

        # (1) the registry index must be resolved before anything is checked.
        # Only a workflow that actually checks needs the index; a workflow
        # that never calls `moon check` is not affected either way.
        #
        # The positions are the positions of the COMMANDS, not of the words.
        # A job called "moon check", or a step named after one, mentions the
        # command without running it, and a mention is not an ordering: what
        # matters is whether the update runs before the check does.
        i_update = None
        i_check = None
        for n, line in lines:
            m = re.match(r"\s*(?:run:\s*)?moon\s+(update|check)\b", line)
            if not m:
                continue
            if m.group(1) == "update":
                if i_update is None:
                    i_update = n
            elif i_check is None:
                i_check = n
        if i_check is not None:
            if i_update is None:
                problems.append(
                    "%s: no `moon update`: a cold runner cannot resolve the "
                    "registry dependencies, so `moon check` fails before it "
                    "checks anything" % name
                )
            elif i_update > i_check:
                problems.append(
                    "%s: `moon update` (line %d) is after the first `moon "
                    "check` (line %d); the index has to be resolved first"
                    % (name, i_update, i_check)
                )

        # (2) no step may cd into a directory its own job already put it in.
        # The directory is resolved PER STEP: a step may override the job's
        # default with its own `working-directory:`, and then that override
        # is what its `cd` is measured against.
        jobs = _workflow_jobs(lines)
        if not jobs:
            jobs = [("(no job block found)", lines)]
        for job_name, job_lines in jobs:
            job_wd = _job_default_workdir(job_lines)
            if job_wd:
                workdirs_seen.add(job_wd)
            steps = _workflow_steps(job_lines)
            if not steps:
                steps = [("(no steps found)", job_lines)]
            for step_label, step_lines in steps:
                step_wd = None
                for _n, line in step_lines:
                    m = re.match(r"\s*working-directory:\s*(\S+)\s*$", line)
                    if m:
                        step_wd = m.group(1).strip("/")
                        break
                if step_wd:
                    workdirs_seen.add(step_wd)
                effective = step_wd if step_wd is not None else job_wd
                if effective is None:
                    continue
                for n, line in step_lines:
                    # `cd mbt` on its own line, and `run: cd mbt` on one
                    # line, are the same mistake. The first version of this
                    # check only matched the first spelling, so the second
                    # one walked straight past the bug this gate exists for.
                    m = re.match(r"\s*(?:run:\s*)?cd\s+(\S+)", line)
                    if not m:
                        continue
                    target = m.group(1).strip("/")
                    if target == effective or target.startswith(effective + "/"):
                        problems.append(
                            "%s job %s step '%s' line %d: `cd %s` under "
                            "`working-directory: %s` asks for %s/%s, which does "
                            "not exist"
                            % (name, job_name, step_label, n, m.group(1),
                               effective, effective, target)
                        )
        audited.append(name)

    if problems:
        return Result("release", "fail", "; ".join(problems), 0,
                      {"problems": problems, "workflows": audited})
    detail = "%d workflow file(s) audited (%s): moon update precedes moon " \
             "check wherever a workflow checks; no `cd` contradicts a " \
             "declared working-directory" % (len(audited), ", ".join(audited))
    if workdirs_seen:
        detail += " (%s)" % ", ".join(sorted(workdirs_seen))
    return Result("release", "pass", detail, 0,
                  {"workflows": audited,
                   "working_directory": sorted(workdirs_seen)})


def gate_golden():
    """Can every committed oracle golden still be produced by the generator beside it?

    A golden is the expected answer, so a stale one is not a stale test -- it is
    a wrong answer that keeps passing. Nothing connected the six checked-in
    goldens to the five checked-in generators, and when the link was finally
    made, two of the generators turned out to have been dead code since the
    day they were written: their `parse()` demanded a `TNORACLE bx` line that
    no file in this repository has ever emitted, in any commit of the history.
    So the block and SE goldens could not be regenerated at all.

    Four of the six now reproduce byte for byte. The other two do not, because
    PyTorch's FFT and convolutions are not bit-reproducible across builds, so
    the rule for those is that every element must agree with a fresh
    regeneration to within the SAME bound the MoonBit test applies to that
    array -- the tolerance the golden is used at, not one fitted to whatever
    comes out.

    This needs PyTorch and `moon`, so it is LOCAL: a missing tool reports
    `unavailable`, because "did not check" is a finding and reporting it as a
    success is the failure this project has paid for before.
    """
    script = os.path.join(ROOT, "verify", "golden_repro.py")
    if not os.path.isfile(script):
        return Result("golden", "fail",
                      "verify/golden_repro.py not found", 0)
    rc, text, dt = run([sys.executable, script], timeout=3600)
    if rc == 2:
        first = ""
        for ln in text.splitlines():
            if ln.strip():
                first = ln.strip()
                break
        return Result("golden", "unavailable",
                      first or "the tools needed to regenerate the goldens "
                               "are not installed", dt)
    m = re.search(r"(\d+) passed, (\d+) failed", text)
    if rc != 0 or not m:
        bad = [ln.strip() for ln in text.splitlines() if ln.startswith("[FAIL")]
        return Result("golden", "fail",
                      "; ".join(bad[:2]) or "the golden check produced no verdict",
                      dt)
    passed, failed = int(m.group(1)), int(m.group(2))
    if failed:
        return Result("golden", "fail",
                      "%d of %d goldens do not reproduce" % (failed, passed + failed),
                      dt)
    return Result("golden", "pass",
                  "%d goldens reproduce from their generators" % passed, dt)


PERF_BASELINE = os.path.join(ROOT, "verify", "perf_baseline.json")
BENCH_PKG = os.path.join(MBT, "bench_lin")


def _bench_exe():
    """The release-built benchmark executable, or None.

    A package inside a module is built into the MODULE's `_build`, not its
    own: `mbt/bench_lin` has no `_build` directory of its own and the binary
    appears as `_build/native/release/build/bench_lin/bench_lin.exe`. The
    harness in verify/run_release_bench.ps1 used to look in the package's
    directory and so never found it.
    """
    leaf = os.path.basename(BENCH_PKG)
    rel = os.path.join("native", "release", "build", leaf)
    for name in (leaf + ".exe", leaf):
        cand = os.path.join(MBT, "_build", rel, name)
        if os.path.isfile(cand):
            return cand
    return None


def _bench_measure(runs=3):
    """Run the release benchmark `runs` times; return the per-call ms list."""
    rc, out, dt = run(["moon", "build", "--release", "--target", "native",
                       "./bench_lin"], cwd=MBT, timeout=1800)
    if rc != 0:
        return None, "the release build failed: %s" % out.strip()[-200:]
    exe = _bench_exe()
    if exe is None:
        return None, "the release build produced no executable"
    per_call = []
    for _ in range(runs):
        rc, out, _dt = run([exe], cwd=BENCH_PKG, timeout=600)
        m = re.search(r"bench_delta:\s*(\d+)", out)
        r = re.search(r"bench_reps:\s*(\d+)", out)
        if rc != 0 or not m or not r:
            return None, "the benchmark produced no timing (exit %s)" % rc
        reps = int(r.group(1))
        if reps <= 0:
            return None, "the benchmark reported %d reps" % reps
        per_call.append(int(m.group(1)) / float(reps))
    return per_call, None


def gate_perf():
    """The hot kernel, measured against a budget derived from a measurement.

    This gate used to grep a COMMENT out of `gpu/moon.pkg` for the string
    "98.4 ms" and report PASS. It measured nothing: the number was prose, and
    the budget it printed was never compared with anything. A gate that
    cannot produce a verdict is not a gate.

    It measures now. `mbt/bench_lin` times `linear_forward` at the LeNet5 fc1
    shape -- batch 64, 4000 -> 500, which is 256 MFLOP per call -- in a
    RELEASE build, and the median of three runs is compared with the budget
    in verify/perf_baseline.json.

    The budget is DERIVED, not fitted: it is twice the measured median, so a
    2x regression fails while the observed run-to-run spread (1.25x over nine
    runs) does not. `QA_PERF_BUDGET_MS` overrides it.

    MACHINE-SPECIFIC, and the detail line says so. The baseline was measured
    on one CPU; a slower one reads high, so the measured value is reported
    rather than hidden behind a pass.
    """
    if not os.path.isfile(os.path.join(BENCH_PKG, "main.mbt")):
        return Result("perf", "unavailable",
                      "mbt/bench_lin/main.mbt not found, so there is no "
                      "benchmark to measure")
    if not os.path.isfile(PERF_BASELINE):
        return Result("perf", "fail",
                      "verify/perf_baseline.json is missing, so there is no "
                      "budget to measure against.")
    try:
        with open(PERF_BASELINE, encoding="utf-8") as fh:
            base = json.load(fh)
    except (OSError, ValueError) as exc:
        return Result("perf", "fail",
                      "verify/perf_baseline.json is unreadable: %s" % exc, 0)
    for key in ("per_call_ms", "budget_ms"):
        if key not in base:
            return Result("perf", "fail",
                          "verify/perf_baseline.json has no '%s'" % key, 0)

    budget = float(os.environ.get("QA_PERF_BUDGET_MS", base["budget_ms"]))
    started = time.time()
    per_call, err = _bench_measure(runs=3)
    elapsed = time.time() - started
    if per_call is None:
        return Result("perf", "fail", err or "the benchmark did not run", elapsed)

    ordered = sorted(per_call)
    median = ordered[len(ordered) // 2]
    used = median / budget if budget > 0 else float("inf")
    detail = ("median %.1f ms/call (min %.1f, max %.1f) against a budget of "
              "%.0f ms -- %.0f%% of it used; baseline %.1f ms on %s"
              % (median, ordered[0], ordered[-1], budget, 100.0 * used,
                 base.get("per_call_ms", float("nan")),
                 base.get("cpu", "an unnamed machine")))
    if median > budget:
        return Result("perf", "fail",
                      "the hot kernel is slower than the budget. " + detail,
                      elapsed, {"median_ms": median, "budget_ms": budget,
                                "runs": per_call})
    return Result("perf", "pass", detail, elapsed,
                  {"median_ms": median, "budget_ms": budget, "runs": per_call})


def gate_bench():
    """The documented measurement entry point, run end to end.

    `verify/run_release_bench.ps1` is what a human is told to run, and it
    could not run at all. It looked for the executable under the PACKAGE's
    `_build`, while a package inside a module is built into the MODULE's;
    and with PowerShell's `$ErrorActionPreference = 'Stop'`, a native
    command that merely wrote to stderr -- as moon does on every successful
    build -- became a terminating error, so the script died on a build that
    had worked.

    This gate runs it and checks that it produced a timing. It is not the
    same check as `perf`: `perf` compares a measurement against a budget,
    and this one checks that the harness works at all.
    """
    script = os.path.join(ROOT, "verify", "run_release_bench.ps1")
    if not os.path.isfile(script):
        return Result("bench", "unavailable",
                      "verify/run_release_bench.ps1 not found")
    shell = shutil.which("pwsh") or shutil.which("powershell")
    if not shell:
        return Result("bench", "unavailable",
                      "neither pwsh nor powershell is on PATH, so the "
                      "documented harness cannot be run here")
    rc, out, dt = run([shell, "-NoProfile", "-ExecutionPolicy", "Bypass",
                       "-File", script], cwd=ROOT, timeout=1800)
    times = re.findall(r"run\s+\d+:\s+([\d.]+)\s+ms", out)
    if rc != 0 or not times:
        tail = " | ".join([ln.strip() for ln in out.splitlines() if ln.strip()][-3:])
        return Result("bench", "fail",
                      "the harness produced no timing (exit %s): %s"
                      % (rc, tail[:300]), dt)
    vals = [float(v) for v in times]
    return Result("bench", "pass",
                  "the release harness ran and reported %s ms (wall clock, "
                  "including process start)" % ", ".join("%.0f" % v for v in vals),
                  dt, {"wall_ms": vals})


PIN_BASELINE = os.path.join(ROOT, "verify", "pin_baseline.json")


def _moon_mod_pins():
    """The module pins in mbt/moon.mod: {module: version}.

    Only the import block is read. The rest of the file carries a
    description and a keyword list, and a scan of the whole file would pick
    up anything in them that happens to look like a pin.
    """
    path = os.path.join(MBT, "moon.mod")
    try:
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
    except OSError as exc:
        return None, "mbt/moon.mod is unreadable: %s" % exc
    m = re.search(r"import\s*\{(.*?)\}", text, re.S)
    if not m:
        return None, "mbt/moon.mod has no import block"
    pins = {}
    for pin in re.finditer(r'"([^"\s@]+)@([^"\s]+)"', m.group(1)):
        pins[pin.group(1)] = pin.group(2)
    if not pins:
        return None, "mbt/moon.mod's import block pins nothing"
    return pins, None


def _registry_latest(module):
    """The registry's latest version for a module, or None if unreachable."""
    url = "https://mooncakes.io/api/v0/modules/" + module
    try:
        with urllib.request.urlopen(url, timeout=30) as fh:
            doc = json.loads(fh.read().decode("utf-8"))
    except (urllib.error.URLError, OSError, ValueError):
        return None
    return doc.get("latest_version")


def gate_pin():
    """Are the pinned dependencies the current ones?

    The release policy is "upgrade the dependencies to their latest version,
    THEN run the gates against them", and nothing enforced it. The reason it
    matters is the failure this repository has already paid for twice: a
    published dependency that cannot compile against the current core. The
    v0.166.1 release was cut against `riantr/moonbit_image` 0.3.9 while 0.3.9
    happened to be the latest -- luck, not a check.

    The registry is the authority. mooncakes.io's module API reports
    `latest_version` for every module, and every pin in `mbt/moon.mod` has to
    equal it.

    A dependency deliberately held back is recorded in
    verify/pin_baseline.json with a reason, and this gate says so in its
    detail instead of failing silently. An exception whose recorded version
    no longer matches what moon.mod pins is itself a finding -- a stale
    exception is an excuse nobody is reading.
    """
    pins, err = _moon_mod_pins()
    if pins is None:
        return Result("pin", "fail", err, 0)

    exceptions = {}
    if os.path.isfile(PIN_BASELINE):
        try:
            with open(PIN_BASELINE, encoding="utf-8") as fh:
                doc = json.load(fh)
            for ex in doc.get("exceptions") or []:
                if ex.get("module") and ex.get("version"):
                    exceptions[ex["module"]] = ex
        except (OSError, ValueError) as exc:
            return Result("pin", "fail",
                          "verify/pin_baseline.json is unreadable: %s" % exc, 0)
    else:
        return Result("pin", "fail",
                      "verify/pin_baseline.json is missing, so a dependency "
                      "held back on purpose could not be recorded.", 0)

    stale = [m for m, ex in exceptions.items()
             if pins.get(m) != ex.get("version")]
    if stale:
        return Result("pin", "fail",
                      "these exceptions no longer match what mbt/moon.mod "
                      "pins: %s" % ", ".join(stale), 0)

    behind = []
    unreachable = []
    checked = 0
    for module, pinned in sorted(pins.items()):
        if module in exceptions:
            continue
        latest = _registry_latest(module)
        if latest is None:
            unreachable.append(module)
            continue
        checked += 1
        if latest != pinned:
            behind.append("%s is pinned at %s and the registry has %s"
                          % (module, pinned, latest))

    if unreachable and checked == 0:
        return Result("pin", "unavailable",
                      "the registry could not be reached for %s, so no pin "
                      "was checked" % ", ".join(unreachable), 0)

    held = ", ".join("%s at %s (%s)" % (m, ex["version"],
                                       ex.get("reason", "no reason recorded"))
                     for m, ex in sorted(exceptions.items()))
    detail = "%d pin(s) checked against the registry" % checked
    if held:
        detail += "; held back on purpose: " + held
    if unreachable:
        detail += "; the registry could not be reached for %s" % ", ".join(unreachable)

    if behind:
        return Result("pin", "fail",
                      "; ".join(behind) + ". " + detail, 0)
    return Result("pin", "pass", detail, 0)


GATES = [
    ("fmt", gate_fmt, False),
    ("lint", gate_lint, False),
    ("deps", gate_deps, False),
    ("pin", gate_pin, False),
    ("dup", gate_dup, False),
    ("shadow", gate_shadow, False),
    ("security", gate_security, False),
    ("oracle", gate_oracle, False),
    ("provenance", gate_provenance, False),
    ("release", gate_release, False),
    ("golden", gate_golden, True),
    ("unit", gate_unit, False),
    ("docs", gate_docs, False),
    ("integration", gate_integration, False),
    ("fuzz", gate_fuzz, False),
    ("bench", gate_bench, True),
    ("perf", gate_perf, True),
    ("coverage", gate_coverage, True),
    ("stress", gate_stress, True),
    ("mutation", gate_mutation, True),
]

# The gates that can run on a CI runner: no MoonBit toolchain, no PyTorch,
# no local build state, and nothing that depends on the speed of the machine
# they are measured on.
#
# These are the gates that check the PROCESS -- the workflows, the dependency
# pins, the documented counts, the shape of the tree -- and the release
# workflow cannot check itself: it is the thing being checked, from the
# inside, where it cannot see its own path.
#
# The gates that need `moon` or `torch` are deliberately NOT here. A runner is
# a colder and a slower machine than the one these baselines were measured
# on, the performance budget is derived from a local median, and the oracle
# and golden gates need a two-gigabyte dependency installed. A gate that fails
# for environmental reasons teaches everyone to ignore red, and three
# releases in this repository have already died to a pipeline problem rather
# than to a content one.
#
# `provenance` is not here either, and its own docstring says why: it audits
# the oracle against a checkout of the reference under `_build/`, which is
# gitignored. On a runner there is nothing to audit against, so it reports
# `unavailable` -- correctly, and fatally, since a CI gate that could not
# check anything fails the job. That is the rule working, not the rule
# being wrong: the first version of this list DID include it, and the first
# CI run failed on exactly this.
CI_GATES = [
    "deps",
    "pin",
    "dup",
    "shadow",
    "security",
    "release",
    "docs",
]


def main():
    ap = argparse.ArgumentParser(description="one loop over this project's QA gates")
    ap.add_argument("gates", nargs="*", help="gate names to run (default: all)")
    ap.add_argument("--list", action="store_true", help="list the registry")
    ap.add_argument("--json", action="store_true", help="emit JSON")
    ap.add_argument("--quick", action="store_true", help="skip slow gates")
    ap.add_argument("--ci", action="store_true",
                    help="run the CI-portable subset, and fail on a gate that "
                         "could not check anything")
    args = ap.parse_args()

    if args.list:
        for name, fn, slow in GATES:
            print(f"  {name:<13} {'(slow)' if slow else '':<7} {fn.__doc__.splitlines()[0]}")
        return 0

    known = {n for n, _, _ in GATES}
    missing = [n for n in CI_GATES if n not in known]
    if missing:
        # A typo here would silently shrink the set of gates CI runs, and a
        # CI job that quietly checks less is worse than one that checks
        # nothing at all, because it looks like coverage.
        print("CI_GATES names gate(s) that do not exist: %s"
              % ", ".join(missing), file=sys.stderr)
        return 2

    selected = GATES
    if args.gates:
        wanted = set(args.gates)
        unknown = wanted - known
        if unknown:
            print(f"unknown gate(s): {', '.join(sorted(unknown))}", file=sys.stderr)
            return 2
        selected = [g for g in GATES if g[0] in wanted]
    elif args.ci:
        selected = [g for g in GATES if g[0] in CI_GATES]
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

    if args.ci and na:
        # In CI, a gate that could not check anything is a failure. Locally an
        # `unavailable` gate is reported and the loop still exits 0, because a
        # missing local toolchain is the developer's business. On a runner
        # there is no developer standing there to fix it, so "did not check"
        # must not read as "passed".
        print("::error::%d gate(s) could not check anything. A CI run that "
              "checks nothing is not a pass." % len(na))
        return 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())