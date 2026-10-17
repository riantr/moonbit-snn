# gate_canary.py -- prove the BatchNorm1d gates can FAIL.
#
# The gates went green. A gate that has only ever been green measures nothing, so
# this deliberately breaks each one and records that it went red. Every mutation
# is applied to a scratch COPY of the tree, and the real file is never touched.
#
#   canary 0  the tree as committed             -> must be GREEN
#   canary 1  one-pass variance restored        -> must be RED (G1, G2)
#   canary 2  the sweep holds only `Correct`    -> must be RED (G3's third
#                                                  assertion is the one that
#                                                  exists to catch this)
#
# canary 2 is the subtle one. Removing every mutant makes the sweep vacuous and
# `missed_by_fd` fall to 0, which is precisely the condition G3 asserts against
# -- so it fails on the assertion that was written to protect the goldens.
#
# Run:  python verify/gate_canary.py

import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "mbt", "nn", "batch_norm2d.mbt")
TEST = os.path.join(ROOT, "mbt", "tests", "wave1", "batch_norm1d_test.mbt")
MOON = os.path.join(os.path.expanduser("~"), ".moon", "bin", "moon.exe")

# The block to swap out. Anchored on its first and last statements with the
# indentation CAPTURED rather than assumed -- a hard-coded indent is a canary
# that silently stops canarying the moment `moon fmt` changes it, which is the
# one thing this file exists to prevent.
TWO_PASS_RE = re.compile(
    r"( *)let mut sum_sq = 0\.0F\n.*?let v = sum_sq / Float::from_int\(nhw\)\n",
    re.S,
)

# The ASP context-layout defect, as it was written. `i0 = base + ti` puts batch
# b's x-slot at x's stride of `c` instead of ctx's stride of `3c`, so batches 1
# and 2 overwrite batch 0's mean and sd slots.
ASP_TARGET = os.path.join(ROOT, "mbt", "tarnet", "tarnet_asp.mbt")
ASP_BUGGY = "        let i0 = base + ti\n"
ASP_FIXED = "        let i0 = (b * 3 * c + ch) * t + ti\n"

# The ASP attention's BatchNorm1d affine, in the order `with_gamma_beta` takes
# it. Swapping the two is the most likely hand-transcription error when a layer
# is inserted, and it is entirely self-consistent: the forward still runs, the
# cache is still well formed, the gradients still flow. Nothing but a value
# comparison against an independent reference catches it, which is why canary 4
# exists.
#
# The match is made on a regex rather than a literal so it survives CRLF: an
# anchor written with "\n" against a CRLF file simply never matches, and a
# canary that silently finds nothing is worse than no canary at all.
BN_RE = re.compile(
    r"( *)p\.att_bn_gamma\.copy\(\),(?:\r?\n)\1p\.att_bn_beta\.copy\(\),"
)


def swap_bn_affine(text):
    if not BN_RE.search(text):
        sys.exit("canary 4: the ASP BatchNorm affine call was not found in its "
                 "expected form; tarnet_asp.mbt changed and this canary needs "
                 "updating before it can be trusted")

    def simple(m):
        nl = "\r\n" if "\r\n" in m.group(0) else "\n"
        ind = m.group(1)
        return (ind + "p.att_bn_beta.copy()," + nl
                + ind + "p.att_bn_gamma.copy(),")

    return BN_RE.sub(simple, text, count=1)


def swap_in_two_pass(text):
    if not TWO_PASS_RE.search(text):
        sys.exit("canary: the two-pass variance block was not found; "
                 "batch_norm2d.mbt has been restructured and this canary "
                 "needs updating before it can be trusted")
    return TWO_PASS_RE.sub(lambda m: one_pass(m.group(1)), text, count=1)


def one_pass(ind):
    return (
        "%(i)slet mut sum_sq = 0.0F\n"
        "%(i)sfor ni in 0..<n {\n"
        "%(i)s  for hi in 0..<h {\n"
        "%(i)s    for wi in 0..<w {\n"
        "%(i)s      let v = input[ni * chw + ci * h * w + hi * w + wi]\n"
        "%(i)s      sum_sq = sum_sq + v * v\n"
        "%(i)s    }\n"
        "%(i)s  }\n"
        "%(i)s}\n"
        "%(i)slet v = sum_sq / Float::from_int(nhw) - m * m\n"
    ) % {"i": ind}




def run_wave1(work):
    # encoding is pinned explicitly: `moon test` draws its output with box
    # characters, and the default codec on this machine is GBK, which cannot
    # decode them and raises UnicodeDecodeError inside subprocess's reader
    # thread -- which then hands back stdout=None and crashes the caller.
    p = subprocess.run(
        [MOON, "test"],
        cwd=os.path.join(work, "mbt", "tests", "wave1"),
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        timeout=2400,
    )
    out = (p.stdout or "") + (p.stderr or "")
    return summarise(out)


def run_wave2(work):
    p = subprocess.run(
        [MOON, "test"],
        cwd=os.path.join(work, "mbt", "tests", "wave2"),
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        timeout=2400,
    )
    out = (p.stdout or "") + (p.stderr or "")
    return summarise(out)


def summarise(out):
    m = re.search(r"Total tests: (\d+), passed: (\d+), failed: (\d+)", out)
    if m:
        return "total %s, passed %s, FAILED %s" % m.groups()
    fails = re.findall(r"FAILED: (A\d\w*:[^\\n]{0,90})", out)
    return "no summary; build/compile failure. first: %s" % (fails[:2] or out[-500:])


TOUCHED = [
    os.path.join("mbt", "nn", "batch_norm2d.mbt"),
    os.path.join("mbt", "tarnet", "tarnet_asp.mbt"),
    os.path.join("mbt", "tests", "wave1", "batch_norm1d_test.mbt"),
    os.path.join("mbt", "tests", "wave2", "block_oracle_test.mbt"),
    os.path.join("mbt", "tests", "wave2", "block_oracle_golden.mbt"),
    os.path.join("mbt", "tests", "wave2", "se_oracle_test.mbt"),
    os.path.join("mbt", "tests", "wave2", "se_oracle_golden.mbt"),
]


def restore(work):
    """Put every file any canary touches back to its committed state.

    Without this the canaries compound. An earlier version of this harness
    restored only the one file the previous canary had edited, so canary 4's
    swapped ASP affine was still in place when canary 5 and canary 6 ran, and
    both reported THAT canary's five failures while their own mutations were
    ineffective. A canary that cannot be trusted to isolate its own defect is
    worse than no canary, because it reads as a passing check.
    """
    for rel in TOUCHED:
        shutil.copyfile(os.path.join(ROOT, rel), os.path.join(work, rel))


def perturb_first_value(text, array_name, factor):
    """Scale the first literal of a generated golden array.

    Used instead of loosening a tolerance. Tolerance-loosening only fails when
    the values are large enough for the loosened bound to be exceeded, which is
    not true here: the SE's output and d_x are O(1) and agree to about 1e-5, so
    no attainable atol makes that gate red. Perturbing the reference itself
    asks the question the gate actually exists to answer -- does it notice when
    the numbers it is compared against are wrong.
    """
    pat = re.compile(
        r"(pub let %s : Array\[Float\] = \[\s*\n?\s*)(-?[\d.]+F?)" % array_name
    )
    m = pat.search(text)
    if not m:
        sys.exit("could not find %s to perturb; the golden's shape changed"
                 % array_name)
    val = float(m.group(2).rstrip("F"))
    return text[:m.start(2)] + ("%.9gF" % (val * factor)) + text[m.end(2):]


def main():
    for p in (TARGET, TEST, MOON):
        if not os.path.exists(p):
            sys.exit("missing: %s" % p)

    original_bn = open(TARGET, encoding="utf-8").read()
    tmp = tempfile.mkdtemp(prefix="bn1d_canary_")
    work = os.path.join(tmp, "repo")
    print("scratch copy: %s" % work)
    shutil.copytree(
        ROOT, work,
        ignore=shutil.ignore_patterns("_build", ".mooncakes", ".git", "_scratch"),
    )
    c_bn = os.path.join(work, "mbt", "nn", "batch_norm2d.mbt")
    c_test = os.path.join(work, "mbt", "tests", "wave1", "batch_norm1d_test.mbt")
    c_asp = os.path.join(work, "mbt", "tarnet", "tarnet_asp.mbt")
    c_blk_gold = os.path.join(work, "mbt", "tests", "wave2",
                              "block_oracle_golden.mbt")
    c_se_gold = os.path.join(work, "mbt", "tests", "wave2",
                            "se_oracle_golden.mbt")

    try:
        print()
        print("canary 0  tree as committed          -> %s" % run_wave1(work))

        # canary 1: put the defect G1 caught back into batch_norm2d.
        restore(work)
        with open(c_bn, "w", encoding="utf-8", newline="") as fh:
            fh.write(swap_in_two_pass(original_bn))
        print("canary 1  one-pass variance restored -> %s" % run_wave1(work))

        # canary 2: empty the mutation sweep.
        restore(work)
        ttext = open(c_test, encoding="utf-8", newline="").read()
        full = ("    Correct,\n    NoEps,\n    NoMeanSub,\n    NoSqrt,\n    NoGamma,\n"
                "    NoBeta,\n    SwapAB,\n    PerSample,\n    Unbiased,\n")
        if full not in ttext:
            sys.exit("canary 2: could not find the mutant list to empty")
        with open(c_test, "w", encoding="utf-8", newline="") as fh:
            fh.write(ttext.replace(full, "    Correct,\n"))
        print("canary 2  sweep holds only Correct    -> %s" % run_wave1(work))

        # canary 3: put the ASP context-layout defect back.
        restore(work)
        asp_src = open(ASP_TARGET, encoding="utf-8").read()
        if ASP_FIXED not in asp_src:
            sys.exit("canary 3: the ASP context index was not found in its "
                     "fixed form; tarnet_asp.mbt changed and this canary needs "
                     "updating before it can be trusted")
        with open(c_asp, "w", encoding="utf-8", newline="") as fh:
            fh.write(asp_src.replace(ASP_FIXED, ASP_BUGGY, 1))
        print("canary 3  ASP ctx stride reverted     -> %s" % run_wave2(work))

        # canary 4: swap the ASP BatchNorm's gamma and beta.
        restore(work)
        with open(c_asp, "w", encoding="utf-8", newline="") as fh:
            fh.write(swap_bn_affine(asp_src))
        print("canary 4  ASP BatchNorm gamma/beta   -> %s" % run_wave2(work))

        # canary 5: the block oracle's reference is perturbed, not its bound.
        restore(work)
        bg = open(c_blk_gold, encoding="utf-8", newline="").read()
        with open(c_blk_gold, "w", encoding="utf-8", newline="") as fh:
            fh.write(perturb_first_value(bg, "blk0_out_golden", 1.05))
        print("canary 5  block oracle golden skewed -> %s" % run_wave2(work))

        # canary 6: the same for the SE oracle's reference.
        restore(work)
        sg = open(c_se_gold, encoding="utf-8", newline="").read()
        with open(c_se_gold, "w", encoding="utf-8", newline="") as fh:
            fh.write(perturb_first_value(sg, "se_out_golden", 1.05))
        print("canary 6  SE oracle golden skewed    -> %s" % run_wave2(work))
    finally:
        with open(TARGET, "w", encoding="utf-8", newline="") as fh:
            fh.write(original_bn)
        shutil.rmtree(tmp, ignore_errors=True)

    print()
    print("canaries 1 to 6 must each report FAILED > 0, and only its own.")
    print("The real files were never modified; the scratch copy is deleted.")


if __name__ == "__main__":
    main()
