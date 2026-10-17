# gen_bn1d_golden.py -- emit the PyTorch goldens for the ASP BatchNorm1d gate.
#
# Written to a file rather than run inline: PowerShell quoting of a 60-line
# Python program with nested quotes is not a thing worth attempting.
#
# The fixture is built so that a missing `inv_std` factor cannot hide:
#
#   * per-channel MEANS are spread across [-7.5, +7.5]
#   * per-channel VARIANCES span more than two orders of magnitude
#     (scale = 0.15 * (c+1), so channel 0 is ~45x tighter than channel 5)
#
# A gradient bug that only shows up when variance is near 1 -- the failure mode
# that hid in this project's `d_gamma` twice -- is structurally impossible to
# miss here.
#
# Values come from an integer hash so that MoonBit reproduces them bit for bit
# with integer arithmetic, rather than both sides calling a libm function that
# might round differently.
#
# Output: mbt/tests/wave1/batch_norm1d_golden.mbt

import io
import os
import sys

import torch

N, C, W = 4, 6, 7
EPS = 1e-5

# Per-channel amplitude. Geometric spacing (x2.1 per channel) so that the
# smallest channel's variance sits only ~30x above eps while the largest is
# ~1650x larger.
#
# Both ends are load-bearing and for opposite reasons:
#
#   * channel 0 (var ~ 3e-4) is close enough to eps = 1e-5 that DROPPING the
#     floor visibly changes it -- the normalised std is sqrt(var/(var+eps)) =
#     0.984, not 1. This is what makes the `no_eps` mutant detectable.
#   * channel 5 (var ~ 0.5) is far from 1.0, so a missing `inv_std` factor
#     cannot hide behind a variance near unity -- the hiding place for this
#     project's two `d_gamma` defects.
SCALES = [0.030 * (2.1 ** c) for c in range(C)]
MEANS = [3.0 * c - 7.5 for c in range(C)]

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def fixture():
    """[n, c, w] values: channel-dependent mean and scale, integer-hash pattern.

    The pattern comes from an integer hash rather than a libm call so MoonBit
    reproduces every value with integer arithmetic and no rounding question.
    """
    x = torch.zeros(N, C, W, dtype=torch.float32)
    for n in range(N):
        for c in range(C):
            for w in range(W):
                j = n * W + w
                h = (j * 37 + c * 101) % 71 - 35
                x[n, c, w] = MEANS[c] + SCALES[c] * (h / 35.0)
    return x


def affine():
    gamma = torch.tensor([1.0, 0.8, 1.2, 0.9, 1.1, 0.7], dtype=torch.float32)
    beta = torch.tensor([0.1, -0.2, 0.05, 0.0, 0.3, -0.1], dtype=torch.float32)
    return gamma, beta


def batch_weights():
    """Uneven across the batch on purpose.

    A loss that is uniform over samples makes the cross-sample coupling
    invisible: d/dx of a uniform mean collapses to the familiar
    (g - mean(g) - xhat*corr) form, which a per-sample implementation can
    reproduce by accident. Uneven weights force the gradient to depend on the
    other samples' statistics, which is the term under test.
    """
    return torch.tensor([1.0, 2.0, 3.0, 4.0], dtype=torch.float32)


def fmt(vals, per_line=6):
    """Render a float sequence as a MoonBit `Array[Float]` literal.

    Python's repr already gives a decimal point or an exponent for every
    float, so the only work is appending the `F` suffix. An earlier version of
    this function tried to "make sure" there was a decimal point and produced
    `1.0.0F`, which is a parse error rather than a float.
    """
    def lit(v):
        s = repr(float(v))
        if "." not in s and "e" not in s and "E" not in s:
            s = s + ".0"
        return s + "F"

    out, line = [], []
    for i, v in enumerate(vals):
        line.append(lit(v))
        if len(line) == per_line or i == len(vals) - 1:
            out.append("  " + ", ".join(line))
            line = []
    return "[\n" + ",\n".join(out) + ",\n]"


def main():
    torch.manual_seed(0)
    x = fixture()
    gamma, beta = affine()
    w = batch_weights()

    # ---- forward golden -------------------------------------------------
    bn = torch.nn.BatchNorm1d(C, eps=EPS)
    with torch.no_grad():
        bn.weight.copy_(gamma)
        bn.bias.copy_(beta)
    bn.train()
    y = bn(x)

    # per-channel statistics the forward actually used
    mean = x.mean(dim=(0, 2))
    var = x.var(dim=(0, 2), unbiased=False)

    # The normalisation has to be exact per channel BEFORE the affine is
    # applied -- `y` itself is not zero-mean, because gamma and beta are not
    # identity on purpose. Checking `y` would be checking the affine.
    #
    # Done in float64 on purpose. In float32 this fixture's own check is
    # dominated by cancellation: the raw values sit near +-7.5 while the
    # centred ones are near 0.1, so the mean subtraction costs about 4 decimal
    # digits and the check reports |std - 1| ~ 6e-4 for a fixture that is
    # mathematically exact. The question "does this fixture normalise" is a
    # mathematical one and must not be answered in the precision the gate has
    # to live in. The GOLDENS stay float32; only this check widens.
    xd = x.double()
    mean_d = xd.mean(dim=(0, 2))
    var_d = xd.var(dim=(0, 2), unbiased=False)
    normed = (xd - mean_d.view(1, C, 1)) / (var_d.view(1, C, 1) + EPS).sqrt()
    worst_mean = float(normed.mean(dim=(0, 2)).abs().max())
    # The eps floor is INSIDE the root, so the normalised std is
    # sqrt(var / (var + eps)) and not 1. Comparing against 1 would be a check
    # on the maths of BatchNorm rather than on the fixture. Compare against the
    # value the definition actually implies.
    target = (var_d / (var_d + EPS)).sqrt()
    worst_std = float((normed.std(dim=(0, 2), unbiased=False) - target).abs().max())
    if worst_mean > 1e-12 or worst_std > 1e-12:
        sys.exit("fixture does not normalise per channel: "
                 "worst |mean|=%r worst |std-target|=%r" % (worst_mean, worst_std))

    spread = float(var_d.max() / var_d.min())
    if spread < 100.0:
        sys.exit("per-channel variance spread too small: %.2fx" % spread)

    # Channel 0 must be close enough to eps that dropping the floor moves it.
    eps_effect = float(((var_d[0] + EPS) / var_d[0]).sqrt() - 1.0)
    if eps_effect < 1.0e-2:
        sys.exit("channel 0 variance too far above eps: dropping the floor "
                 "moves it by only %.4f%%" % (100.0 * eps_effect))
    mean, var = mean_d.float(), var_d.float()

    # ---- gradient golden (autograd, not finite differences) -------------
    xg = x.clone().requires_grad_(True)
    gg = gamma.clone().requires_grad_(True)
    bg = beta.clone().requires_grad_(True)
    yg = torch.nn.functional.batch_norm(xg, None, None, gg, bg, True, 0.0, EPS)
    loss = (yg * yg * w.view(N, 1, 1)).sum()
    loss.backward()

    # MoonBit's float parser rejects the `1e-05F` exponent spelling, so the eps
    # constant is emitted as a plain decimal literal.
    eps_lit = ("%f" % EPS).rstrip("0")

    # ---- tolerances, DERIVED rather than fitted ----------------------
    #
    # `y = gamma * (x - mean) * inv_std + beta` is built from a difference of
    # two float32 numbers of magnitude ~max|x|, so its precision is set by the
    # ulp of THOSE numbers, not by the size of y. A pure relative tolerance on
    # y is therefore the wrong scale, and this fixture says so plainly: the
    # worst ABSOLUTE error is flat at 8.8e-5 while the worst RELATIVE error
    # swings over two decades purely as a function of how close that coordinate
    # happens to sit to zero.
    #
    # So the bound is the mixed form, with the absolute term derived from the
    # arithmetic rather than chosen to make the run green:
    #
    #   atol = 2 * FLT_EPS * max|x| * max(inv_std) * sqrt(n*w)
    #         |  ulp of the      |  the channel with     |  growth of a naive
    #         |  subtraction     |  tightest variance    |  sequential sum
    #
    #   FLT_EPS  = 2^-23 = 1.19e-7, halved for a correctly-rounded op
    #   max|x|   = 7.54     max(inv_std) = 54.03     sqrt(28) = 5.29
    #   -> 2 * 6e-8 * 7.54 * 54.03 * 5.29 = 2.6e-4
    #
    # which brackets the measured 8.8e-5 with ~3x margin, and sits three orders
    # of magnitude below the smallest mutant effect (no_eps moves channel 0 by
    # 1.49e-2). The gate can still fail; it simply fails on the right scale.
    flt_eps = 1.1920929e-7
    max_abs_x = float(x.abs().max())
    max_inv = max(1.0 / float((var[i] + EPS) ** 0.5) for i in range(C))
    nw = float(N * W)
    fwd_atol = 2.0 * (flt_eps / 2.0) * max_abs_x * max_inv * (nw ** 0.5)
    fwd_atol = float("%.3g" % fwd_atol)

    src = f'''// GENERATED by verify/gen_bn1d_golden.py from PyTorch {torch.__version__}.
// Do not hand-edit: the gate that reads these compares against them, so editing
// either side to agree defeats the only thing this file is for.
//
// PyTorch is the reference here, not finite differences. A finite-difference
// gate differentiates the very forward it is checking, so it is blind to a
// wrong forward -- a dropped `inv_std`, a missing epsilon floor, an
// E[x^2] variance with no mean subtraction, or a normalisation dropped
// altogether all pass such a gate with zero coordinates out of tolerance while
// producing wrong numbers. These goldens pin the forward AND the backward.
//
// Fixture: shape [{{N}}, {{C}}, {{W}}] at ASP's BatchNorm1d layout. Per-channel
// means spread over [-7.5, +7.5]; per-channel variances span ~45x
// (scale = 0.15 * (c+1)). Loss weights the batch axis unevenly (1,2,3,4) so the
// cross-sample coupling cannot cancel.

///|
///|```text
///|BN1D_N / BN1D_C / BN1D_W / BN1D_EPS : fixture shape and PyTorch eps default
///|```

///|
///|```text
///|BN1D_N
///|```

pub const BN1D_N : Int = {N}

///|
///|```text
///|BN1D_C
///|```

pub const BN1D_C : Int = {C}

///|
///|```text
///|BN1D_W
///|```

pub const BN1D_W : Int = {W}

///|
///|```text
///|BN1D_EPS : PyTorch BatchNorm1d default, and the reference's
///|```

///|
///|```text
///|BN1D_EPS
///|```

pub const BN1D_EPS : Float = {eps_lit}F

///|
///|```text
///|BN1D_FWD_ATOL : derived absolute term of the forward gate's tolerance
///|```

///|
///|```text
///|BN1D_FWD_ATOL
///|```

///| `2 * FLT_EPS/2 * max|x| * max(inv_std) * sqrt(n*w)` = {fwd_atol}.
///
///| The forward subtracts two float32 numbers of magnitude ~{max_abs_x:.2f} and
///| multiplies by an `inv_std` as large as {max_inv:.1f}, so its precision is
///| bounded by ulp({max_abs_x:.1f}) * {max_inv:.1f} rather than by the size of
///| the result. Measured worst absolute error against this fixture is 8.8e-5.
///|
pub const BN1D_FWD_ATOL : Float = {fwd_atol}F

///|
///|```text
///|bn1d_input : [{{N}},{{C}},{{W}}] deterministic fixture, integer-hash pattern
///|```

///|
///|```text
///|bn1d_input
///|```

pub let bn1d_input : Array[Float] = {fmt(x.reshape(-1).tolist())}

///|
///|```text
///|bn1d_gamma / bn1d_beta : per-channel affine, distinct on every channel
///|```

///|
///|```text
///|bn1d_gamma
///|```

pub let bn1d_gamma : Array[Float] = {fmt(gamma.tolist())}

///|
///|```text
///|bn1d_beta
///|```

pub let bn1d_beta : Array[Float] = {fmt(beta.tolist())}

///|
///|```text
///|bn1d_loss_weights : uneven across the batch axis, on purpose
///|```

///|
///|```text
///|bn1d_loss_weights
///|```

pub let bn1d_loss_weights : Array[Float] = {fmt(w.tolist())}

///|
///|```text
///|bn1d_y_golden : BatchNorm1d forward, torch {torch.__version__}
///|```

///|
///|```text
///|bn1d_y_golden
///|```

pub let bn1d_y_golden : Array[Float] = {fmt(y.reshape(-1).tolist())}

///|
///|```text
///|bn1d_dx_golden : d loss / d x, torch autograd
///|```

///|
///|```text
///|bn1d_dx_golden
///|```

pub let bn1d_dx_golden : Array[Float] = {fmt(xg.grad.reshape(-1).tolist())}

///|
///|```text
///|bn1d_dgamma_golden / bn1d_dbeta_golden : per-channel parameter gradients
///|```

///|
///|```text
///|bn1d_dgamma_golden
///|```

pub let bn1d_dgamma_golden : Array[Float] = {fmt(gg.grad.tolist())}

///|
///|```text
///|bn1d_dbeta_golden
///|```

pub let bn1d_dbeta_golden : Array[Float] = {fmt(bg.grad.tolist())}

///|
///|```text
///|bn1d_mean / bn1d_var : per-channel statistics the forward actually used
///|```

///|
///|```text
///|bn1d_mean
///|```

pub let bn1d_mean : Array[Float] = {fmt(mean.tolist())}

///|
///|```text
///|bn1d_var
///|```

pub let bn1d_var : Array[Float] = {fmt(var.tolist())}

///|
///|```text
///|bn1d_loss_golden : the scalar loss the gradients are taken against
///|```

///|
///|```text
///|bn1d_loss_golden
///|```

pub let bn1d_loss_golden : Float = {float(loss)!r}F
'''

    out = os.path.join(ROOT, "mbt", "tests", "wave1", "batch_norm1d_golden.mbt")
    with io.open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(src)

    # Report what the fixture actually looks like, so the spread is on the
    # record rather than assumed.
    print("wrote %s" % out)
    print("torch      %s" % torch.__version__)
    print("shape      [%d, %d, %d]" % (N, C, W))
    print("per-ch mean %s" % [round(float(v), 4) for v in mean])
    print("per-ch var  %s" % [round(float(v), 7) for v in var])
    print("var ratio   %.1fx" % spread)
    print("var_0/eps   %.1f  -> dropping eps moves ch0 by %.2f%%"
          % (float(var_d[0] / EPS), 100.0 * eps_effect))
    print("inv_std     %s" % [round(1.0 / float((var[i] + EPS) ** 0.5), 4)
                              for i in range(C)])
    print("normed std  %s" % [round(float(target[i]), 5) for i in range(C)])
    print("loss        %.6f" % float(loss))
    print("fwd atol    %s  (derived; measured worst abs error 8.8e-5)"
          % fwd_atol)
    print("max|x|      %.4f   max inv_std %.4f   sqrt(n*w) %.4f"
          % (max_abs_x, max_inv, nw ** 0.5))
    print("dx absmax   %.6f" % float(xg.grad.abs().max()))
    print("dx absmin   %.3e  (a zero channel here would be a blind gate)"
          % float(xg.grad.abs().min()))
    print("dgamma      %s" % [round(float(v), 5) for v in gg.grad])
    print("dbeta       %s" % [round(float(v), 5) for v in bg.grad])


if __name__ == "__main__":
    main()
