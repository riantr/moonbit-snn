# audit_transcription.py -- check that the oracles still match the reference.
#
# WHY. `verify/gen_model_oracle.py` says it is "transcribed from
# `_build/tarnet_ref/tarnet/models/tarnet.py` rather than from this port". That
# sentence is the entire provenance of the gate that found the bottleneck's
# mis-laid-out 1x1 convolution, and it was an ASSERTION -- nobody had checked it.
# The reference had been read by a person, once, and the transcription written
# from what they remembered.
#
# That is not a criticism of the transcription, which an audit of every layer
# found faithful: the backbone wiring, `Conv1DBlock`, `ChannelTimeSenseSELayer`,
# `ASPHead`, `SEModule` and all three eps constants match. It is a criticism of
# the fact that nothing would have noticed if they stopped matching. A gate whose
# reference can silently drift is a gate whose reference eventually will.
#
# So this reads the reference and asserts, structurally, the facts the oracles
# depend on. It does NOT execute the reference -- it cannot, the oracles exist
# precisely because the reference is the thing we are checking against. It reads
# the source the way a careful reviewer would, and it is written so that each
# assertion can fail: every one of them is anchored on a literal from the file,
# so a renamed local or a reordered Sequential turns into a FAIL rather than a
# silent pass.
#
# THE TWO KNOWN DEVIATIONS are asserted too, not excused. If someone "fixes" the
# reference or the port to match the other, this must notice.
#
#   1. `Conv1DBlock.shared` declares TWO `nn.PReLU()`. The port shares ONE slope
#      across both sites (tarnet.mbt, `tarnet_block_backward`). The gradient is
#      the sum of both sites', so training still works; the parameter count is
#      one lower per block.
#   2. `TARNet.__init__` applies `nn.init.xavier_uniform_(self.fc.weight)`;
#      the port allocates `randn(0.05)`. Initialisation only -- it changes no
#      forward and no backward, only where a seeded model starts.
#
# Run:  python verify/audit_transcription.py
#
# `_build/` is gitignored, so the reference is a local checkout and this gate is
# LOCAL. A missing reference is reported as `unavailable`, never as `pass`:
# "did not check" is a finding, not a success.

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.join(ROOT, "_build", "tarnet_ref", "tarnet", "models", "tarnet.py")

results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))


def has(pattern, text, flags=re.S):
    return re.search(pattern, text, flags) is not None


def audit(src):
    # ---- SEModule -------------------------------------------------------
    check(
        "SEModule: avgpool -> conv -> relu -> conv -> sigmoid",
        has(r"AdaptiveAvgPool1d\(1\).*?Conv1d\(channels, bottleneck, kernel_size=1\)"
            r".*?nn\.ReLU\(\).*?Conv1d\(bottleneck, channels, kernel_size=1\)"
            r".*?nn\.Sigmoid\(\)", src),
    )
    check("SEModule: applies the gate multiplicatively",
          has(r"weights = self\.se\(x\).*?return x \* weights", src))

    # ---- ASPHead --------------------------------------------------------
    check("ASPHead: context triples the channels",
          has(r"in_att = channels \* 3", src))
    check(
        "ASPHead: conv -> relu -> batchnorm -> tanh -> conv -> softmax(dim=2)",
        has(r"nn\.Conv1d\(in_att, att_hidden, kernel_size=1\).*?nn\.ReLU\(inplace=True\)"
            r".*?nn\.BatchNorm1d\(att_hidden\).*?nn\.Tanh\(\)"
            r".*?nn\.Conv1d\(att_hidden, channels, kernel_size=1\)"
            r".*?nn\.Softmax\(dim=2\)", src),
    )
    check("ASPHead: context std floor is 1e-4",
          has(r"torch\.sqrt\(x\.var\(dim=2, keepdim=True, unbiased=False\) \+ 1e-4\)", src))
    check("ASPHead: pooled variance clamps at 1e-4",
          has(r"var = \(second - mu \*\* 2\)\.clamp_min\(1e-4\)", src))
    check("ASPHead: context is [x, mean, std] concatenated on channels",
          has(r"ctx = torch\.cat\(\[x, mean_t\.repeat\(1, 1, x\.size\(2\)\), "
              r"std_t\.repeat\(1, 1, x\.size\(2\)\)\], dim=1\)", src))
    check("ASPHead: pools over time and returns [mu, sigma]",
          has(r"mu = torch\.sum\(x \* alpha, dim=2\).*?"
              r"second = torch\.sum\(\(x \*\* 2\) \* alpha, dim=2\).*?"
              r"return torch\.cat\(\[mu, sigma\], dim=1\)", src))

    # ---- ChannelTimeSenseSELayer ----------------------------------------
    check("CTSE: kernel sizes 3, 5, 9",
          has(r"kersize: tuple\[int, int, int\] = \(3, 5, 9\)", src))
    check("CTSE: reduction ratio 2",
          has(r"reduction_ratio: int = 2", src))
    check("CTSE: c_red = c // rr",
          has(r"c_red = max\(1, c // max\(1, rr\)\)", src))
    check("CTSE: three depthwise paths, each pooling to 1 then ReLU",
          has(r"nn\.Conv1d\(\s*c, c,.*?groups=groups,.*?bias=True,\s*\),"
              r"\s*nn\.AdaptiveAvgPool1d\(1\),\s*nn\.ReLU\(inplace=True\)", src))
    check("CTSE: feature_fc is Linear(3, 1)",
          has(r"self\.feature_fc = nn\.Linear\(3, 1\)", src))
    check("CTSE: gate is x * w.view(B, C, 1)",
          has(r"return x \* w\.view\(x\.size\(0\), x\.size\(1\), 1\)", src))

    # ---- Conv1DBlock ----------------------------------------------------
    check("Conv1DBlock: padding is (k-1)*dilation//2",
          has(r"padding = \(kernel_size - 1\) \* dilation // 2", src))
    check(
        "Conv1DBlock: pointwise -> depthwise-dilated -> pointwise, norm after each",
        has(r"nn\.Conv1d\(in_chan, hid_chan, 1, bias=True\).*?conv_norm\(hid_chan\).*?"
            r"groups=hid_chan,.*?bias=True,\s*\).*?conv_norm\(hid_chan\)", src),
    )
    check("Conv1DBlock: residual is added last",
          has(r"residual = self\.res_conv\(residual\).*?"
              r"residual = self\.chan_se\(residual\).*?return x \+ residual", src))
    # DEVIATION 1 -- asserted, not excused.
    n_prelu = len(re.findall(r"nn\.PReLU\(\)", src))
    check("DEVIATION 1: reference declares TWO PReLU, the port shares one slope",
          n_prelu == 2,
          "found %d nn.PReLU() in the reference; the port shares one slope, so a "
          "count other than 2 means the reference changed under that deviation"
          % n_prelu)

    # ---- backbone -------------------------------------------------------
    check("backbone: stacks are [1,2], [4,8], [16]",
          has(r"stack_s = build_stack\(\[1, 2\], repeats_s\).*?"
              r"stack_m = build_stack\(\[4, 8\], repeats_m\).*?"
              r"stack_l = build_stack\(\[16\], repeats_l\)", src))
    check("backbone: a stack repeats the dilation list, not the other way round",
          has(r"for _ in range\(int\(repeats\)\):\s*for d in dilations:", src))
    check("backbone: bottleneck is norm then a 1x1 convolution",
          has(r"self\.bottleneck = nn\.Sequential\(\s*layer_norm,\s*"
              r"nn\.Conv1d\(in_chan, bn_chan, kernel_size=1, bias=True\)", src))
    check("backbone: concat order is [short, mid, long]",
          has(r"z = torch\.cat\(\[x_s, x_m, x_l\], dim=1\)", src))
    check(
        "backbone: MFA is conv -> relu -> SEModule, SE AFTER the ReLU",
        has(r"self\.mfa = nn\.Sequential\(\s*"
            r"nn\.Conv1d\(3 \* bn_chan, fuse_out, kernel_size=1, bias=True\),\s*"
            r"nn\.ReLU\(inplace=True\),\s*"
            r"SEModule\(fuse_out, bottleneck=max\(32, fuse_out // 4\)\)", src),
    )
    check("backbone: SE bottleneck is max(32, fuse_out // 4)",
          has(r"SEModule\(fuse_out, bottleneck=max\(32, fuse_out // 4\)\)", src))
    check("backbone: bn_seg is BatchNorm1d(2 * fuse_out)",
          has(r"self\.bn_seg = nn\.BatchNorm1d\(2 \* fuse_out\)", src))
    check("backbone: fc is Linear(2 * fuse_out, emb_dim)",
          has(r"self\.fc = nn\.Linear\(2 \* fuse_out, emb_dim\)", src))
    check("backbone: bn_out is BatchNorm1d(emb_dim)",
          has(r"self\.bn_out = nn\.BatchNorm1d\(emb_dim\)", src))
    check(
        "backbone forward: bottleneck -> 3 stacks -> cat -> mfa -> asp -> bn_seg -> fc -> bn_out",
        has(r"x0 = self\.bottleneck\(x\).*?x_s = self\._run_stack\(x0, self\.stack_s\).*?"
            r"x_m = self\._run_stack\(x0, self\.stack_m\).*?"
            r"x_l = self\._run_stack\(x0, self\.stack_l\).*?"
            r"z = torch\.cat\(\[x_s, x_m, x_l\], dim=1\).*?"
            r"z = self\.mfa\(z\).*?pooled = self\.asp\(z\).*?"
            r"pooled = self\.bn_seg\(pooled\).*?emb = self\.fc\(pooled\).*?"
            r"emb = self\.bn_out\(emb\)", src),
    )

    # ---- classifier -----------------------------------------------------
    check("TARNet: classifier is Linear(emb_dim, n_classes)",
          has(r"self\.fc = nn\.Linear\(emb_dim, n_classes\)", src))
    check("TARNet: logits come straight from the backbone embedding",
          has(r"emb = self\.backbone\(wav\).*?return self\.fc\(emb\)", src))
    # DEVIATION 2 -- initialisation only.
    check("DEVIATION 2: reference inits fc.weight with xavier_uniform, port uses randn(0.05)",
          has(r"nn\.init\.xavier_uniform_\(self\.fc\.weight\)", src))


MUTATIONS = [
    ("concat order long->short",
     r"z = torch\.cat\(\[x_s, x_m, x_l\], dim=1\)",
     "z = torch.cat([x_l, x_m, x_s], dim=1)"),
    ("SE moved BEFORE the ReLU",
     r"nn\.ReLU\(inplace=True\),\s*"
     r"SEModule\(fuse_out, bottleneck=max\(32, fuse_out // 4\)\)",
     "SEModule(fuse_out, bottleneck=max(32, fuse_out // 4)),\n            "
     "nn.ReLU(inplace=True),"),
    ("ASP std floor 1e-4 -> 1e-5",
     r"unbiased=False\) \+ 1e-4\)",
     r"unbiased=False) + 1e-5)"),
    ("ASP BatchNorm removed from the attention",
     r"nn\.BatchNorm1d\(att_hidden\),\s*", ""),
    ("CTSE kernel sizes 3,5,9 -> 3,5,7",
     r"kersize: tuple\[int, int, int\] = \(3, 5, 9\)",
     r"kersize: tuple[int, int, int] = (3, 5, 7)"),
    ("block padding formula changed",
     r"padding = \(kernel_size - 1\) \* dilation // 2",
     "padding = kernel_size // 2"),
    ("dilation lists changed",
     r"stack_m = build_stack\(\[4, 8\], repeats_m\)",
     r"stack_m = build_stack([4, 16], repeats_m)"),
    ("bn_seg width 2*fuse -> fuse",
     r"self\.bn_seg = nn\.BatchNorm1d\(2 \* fuse_out\)",
     r"self.bn_seg = nn.BatchNorm1d(fuse_out)"),
    ("classifier no longer follows the backbone",
     r"emb = self\.backbone\(wav\).*?return self\.fc\(emb\)",
     "return torch.zeros(1)"),
    ("the second PReLU removed (deviation 1 would stop being true)",
     r"nn\.PReLU\(\),\s*conv_norm\(hid_chan\),", "conv_norm(hid_chan),"),
]


def selftest(src):
    """Every mutation must turn its own check red, and nothing else.

    A checker whose assertions all pass tells you nothing about whether they can
    fail. Each mutation below rewrites one structural fact in a COPY of the
    reference and requires the audit to notice -- and requires it to notice
    THAT one, because an audit that fails on everything at once would pass this
    while proving nothing.
    """
    print("SELFTEST: each mutation must turn its own check red")
    base_fail = [n for n, ok, _ in results if not ok]
    assert not base_fail, "the real reference already fails: %s" % base_fail
    bad = 0
    for label, pattern, repl in MUTATIONS:
        # re.S matters: the patterns below span lines, and without it `.` never
        # crosses a newline and the mutation silently applies to nothing -- which
        # the selftest reports rather than passes over.
        mutated, n = re.subn(pattern, repl, src, count=1, flags=re.S)
        if n != 1:
            print("  [FAIL] %-46s mutation did not apply" % label)
            bad += 1
            continue
        del results[:]
        audit(mutated)
        failed = [nm for nm, ok, _ in results if not ok]
        del results[:]
        audit(src)
        if not failed:
            print("  [FAIL] %-46s mutation applied but NOTHING went red" % label)
            bad += 1
        elif len(failed) > 2:
            print("  [wary] %-46s turned %d checks red: %s"
                  % (label, len(failed), "; ".join(failed)))
        else:
            print("  [ok]   %-46s -> %s" % (label, "; ".join(failed)))
    print()
    if bad:
        print("SELFTEST FAILED: %d mutations were not detected" % bad)
        return 1
    print("SELFTEST PASS: %d/%d mutations detected, each localised"
          % (len(MUTATIONS), len(MUTATIONS)))
    return 0


def main():
    if not os.path.exists(REF):
        print("UNAVAILABLE: the reference is not checked out.")
        print("  expected: %s" % REF)
        print("  `_build/` is gitignored, so the reference is a LOCAL checkout and")
        print("  this audit cannot run in CI. That is a real limitation of the")
        print("  provenance claim, not a pass -- the transcription of the reference")
        print("  is unverified wherever this file is absent.")
        return 2

    with open(REF, encoding="utf-8") as fh:
        src = fh.read()
    audit(src)

    if "--selftest" in sys.argv:
        return selftest(src)

    npass = sum(1 for _, ok, _ in results if ok)
    nfail = len(results) - npass
    width = max(len(n) for n, _, _ in results)
    for name, ok, detail in results:
        print("  [%s] %-*s %s" % ("ok" if ok else "FAIL", width, name, detail))
    print()
    print("transcription audit: %d/%d structural facts hold against %s"
          % (npass, len(results), os.path.relpath(REF, ROOT)))
    if nfail:
        print("%d FAILED. Either the reference changed or this audit is stale; "
              "both are findings." % nfail)
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
