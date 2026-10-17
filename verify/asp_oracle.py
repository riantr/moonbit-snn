# asp_oracle.py -- a PyTorch autograd oracle for TARNet's ASP head.
#
# WHY. `tests/wave2/tarnet_test.mbt` gates ASP's gradients against central
# differences at rtol = 3%, atol = 2e-3, h = 1.2e-2 and it is green. Measured on
# the BatchNorm1d gate, a float32 central difference of an objective summed over
# ~170 terms carries an absolute noise floor of order 0.1 at a 1e-2 step. If
# this objective is of similar magnitude then a 2e-3 atol is far below the
# instrument's own noise, which means the green tells you very little.
#
# A torch autograd reference has no such problem: it is exact to float32
# roundoff, has no step size, and cannot agree with a wrong implementation by
# construction. Comparing against it answers two separate questions at once --
# whether `asp_backward` is right, and how much slack the existing finite-
# difference gate actually has.
#
# `tarnet_asp.mbt` records a long investigation into `asp_backward`'s `d_x`
# ending in NOT SOLVED, with the note that "the chain written in asp_backward is
# not the chain asp_forward computes". This file is how that claim gets settled.
#
# Run:  python verify/asp_oracle.py

import io
import os
import sys

import numpy as np
import torch

N, C, T, ATT = 3, 5, 7, 6
STD_FLOOR = 1.0e-4   # ASP_STD_FLOOR
VAR_FLOOR = 1.0e-4   # ASP_VAR_FLOOR
BN_EPS = 1.0e-5      # the ASP BatchNorm1d's eps, PyTorch's default


def hsh(*terms):
    """Deterministic integer hash -> value in [-1, 1).

    Reproduced with integer arithmetic on the MoonBit side so both languages
    build the identical fixture without sharing an RNG.
    """
    acc = 0
    for i, (k, m) in enumerate(zip(terms, (37, 101, 17, 53, 29, 71, 89))):
        acc += (k + 1) * m
    return ((acc % 71) - 35) / 35.0


def fixture():
    """x [n,c,t] with per-channel distinct mean and spread."""
    x = np.zeros((N, C, T), dtype=np.float32)
    for b in range(N):
        for c in range(C):
            mu_c = 0.7 * c - 2.1
            sd_c = 0.25 + 0.4 * c
            for t in range(T):
                x[b, c, t] = mu_c + sd_c * hsh(b * T + t, c)
    return x


def params(offset=0.0):
    w1 = np.zeros((ATT, 3 * C), dtype=np.float32)
    b1 = np.zeros(ATT, dtype=np.float32)
    w2 = np.zeros((C, ATT), dtype=np.float32)
    b2 = np.zeros(C, dtype=np.float32)
    for j in range(ATT):
        b1[j] = offset + 0.02 * hsh(j, 5, 3)
        for s in range(3 * C):
            w1[j, s] = 0.05 * hsh(j, s, 11)
    for c in range(C):
        b2[c] = 0.02 * hsh(c, 7, 19)
        for j in range(ATT):
            w2[c, j] = 0.05 * hsh(c, j, 23)
    return w1, b1, w2, b2


def pre_activations(x, w1, b1):
    xt = torch.tensor(x)
    mean = xt.mean(dim=2, keepdim=True)
    var = (xt * xt).mean(dim=2, keepdim=True) - mean * mean
    sd = torch.sqrt(var + STD_FLOOR)
    ctx = torch.cat([xt, mean.expand(-1, -1, T), sd.expand(-1, -1, T)], 1)
    return torch.einsum("js,nst->njt",
                        torch.tensor(w1), ctx) + torch.tensor(b1).view(1, -1, 1)


def choose_offset(x):
    """Pick the b1 offset that keeps ReLU away from its kink.

    A pre-activation near zero is a discontinuity: one float32 ulp of
    disagreement between two summation orders flips the ReLU derivative, and
    with it every attention weight gradient. A fixture that sits on the kink
    measures the summation order rather than the mathematics, and it passes or
    fails for reasons that have nothing to do with the code.

    The offset is searched rather than guessed, and the objective is the
    distance to the kink SUBJECT TO both signs still occurring -- a large
    positive offset would maximise the margin by switching ReLU entirely off,
    which tests less.
    """
    best = None
    total_needed = 1
    for off in np.arange(-1.5, 1.501, 0.01):
        w1, b1, _, _ = params(float(off))
        pre = pre_activations(x, w1, b1).detach().numpy()
        pos = int((pre > 0).sum())
        neg = int((pre < 0).sum())
        total_needed = pre.size
        # Both signs must be a real share, not just present. A fixture with 8
        # positives and 118 negatives maximises the margin by nearly switching
        # ReLU off, which is the failure mode the previous offset search walked
        # straight into.
        if pos < 0.25 * total_needed or neg < 0.25 * total_needed:
            continue
        margin = float(np.abs(pre).min())
        if best is None or margin > best[1]:
            best = (float(off), margin, pos, neg)
    if best is None:
        sys.exit("no offset keeps both ReLU signs at >=25%% with a margin")
    return best


def d_out():
    """A fixed non-symmetric incoming gradient, so no symmetry can hide."""
    g = np.zeros((N, 2 * C), dtype=np.float32)
    for b in range(N):
        for k in range(2 * C):
            g[b, k] = hsh(b * 2 * C + k, 31) * 0.9 + 0.1
    return g


def bn_affine():
    """Per-unit BatchNorm1d affine. Distinct on every unit, never all-1/all-0 --
    an identity affine would hide a swapped gamma and beta, which is the single
    most likely transcription error when a layer is inserted by hand."""
    gamma = np.array([1.0 + 0.07 * ((j * 5) % 7 - 3) for j in range(ATT)],
                     dtype=np.float32)
    beta = np.array([0.05 * (((j * 3) % 5) - 2) for j in range(ATT)], dtype=np.float32)
    return gamma, beta


def asp_forward_torch(x, w1, b1, gamma, beta, w2, b2):
    """asp_forward transcribed, differentiable, BatchNorm1d included."""
    n, c, t = x.shape
    att = w1.shape[0]
    mean = x.mean(dim=2, keepdim=True)                      # [n,c,1]
    # ctx variance as written: E[x^2] - mean^2, no floor clamp inside
    var_ctx = (x * x).mean(dim=2, keepdim=True) - mean * mean
    sd = torch.sqrt(var_ctx + STD_FLOOR)
    ctx = torch.cat([x, mean.expand(-1, -1, t), sd.expand(-1, -1, t)], dim=1)
    pre = torch.einsum("js,nst->njt", w1, ctx) + b1.view(1, att, 1)
    relu = torch.relu(pre)
    # BatchNorm1d over [n, att, t]: statistics per attention unit over (n, t).
    xhat = (relu - relu.mean(dim=(0, 2), keepdim=True)) / torch.sqrt(
        relu.var(dim=(0, 2), unbiased=False, keepdim=True) + BN_EPS)
    bn = xhat * gamma.view(1, att, 1) + beta.view(1, att, 1)
    hidden = torch.tanh(bn)
    logits = torch.einsum("cj,njt->nct", w2, hidden) + b2.view(1, c, 1)
    alpha = torch.softmax(logits, dim=2)
    mu = (x * alpha).sum(dim=2)                             # [n,c]
    second = (x * x * alpha).sum(dim=2)                     # [n,c]
    raw = second - mu * mu
    var_out = torch.clamp(raw, min=VAR_FLOOR)
    out = torch.cat([mu, torch.sqrt(var_out)], dim=1)       # [n,2c]
    return out, alpha, ctx


def fmt(vals, per_line=6):
    def lit(v):
        s = repr(float(v))
        if "." not in s and "e" not in s and "E" not in s:
            s += ".0"
        return s + "F"
    out, line = [], []
    for i, v in enumerate(np.asarray(vals).reshape(-1)):
        line.append(lit(v))
        if len(line) == per_line or i == len(np.asarray(vals).reshape(-1)) - 1:
            out.append("  " + ", ".join(line))
            line = []
    return "[\n" + ",\n".join(out) + ",\n]"


def main():
    xf = fixture()
    offset, margin, npos, nneg = choose_offset(xf)
    w1f, b1f, w2f, b2f = params(offset)
    bn_g, bn_b = bn_affine()
    gf = d_out()

    x = torch.tensor(xf, requires_grad=True)
    w1 = torch.tensor(w1f, requires_grad=True)
    b1 = torch.tensor(b1f, requires_grad=True)
    gamma = torch.tensor(bn_g, requires_grad=True)
    beta = torch.tensor(bn_b, requires_grad=True)
    w2 = torch.tensor(w2f, requires_grad=True)
    b2 = torch.tensor(b2f, requires_grad=True)
    g = torch.tensor(gf)

    out, alpha, _ = asp_forward_torch(x, w1, b1, gamma, beta, w2, b2)
    (out * g).sum().backward()

    # Did the ReLU actually pass signal? On the fixture the project used, every
    # pre-activation was negative, which zeroes the whole attention-to-input path
    # and leaves the context coupling untested.
    with torch.no_grad():
        n_, c_, t_ = x.shape
        mean = x.mean(dim=2, keepdim=True)
        var = (x * x).mean(dim=2, keepdim=True) - mean * mean
        sd_ = torch.sqrt(var + STD_FLOOR)
        ctx = torch.cat([x, mean.expand(-1, -1, t_), sd_.expand(-1, -1, t_)], 1)
        pre = torch.einsum("js,nst->njt", w1, ctx) + b1.view(1, w1.shape[0], 1)
        live = int((pre > 0).sum())
        total = int(pre.numel())

    # clamp activity: how many (b,c) land on the variance floor?
    with torch.no_grad():
        mu = (x * alpha).sum(dim=2)
        second = (x * x * alpha).sum(dim=2)
        raw = second - mu * mu
        at_floor = int((raw <= VAR_FLOOR).sum())

    print("n,c,t,att      = %d,%d,%d,%d" % (N, C, T, ATT))
    print("b1 offset      = %.2f, chosen so min |pre| = %.4f" % (offset, margin))
    print("ReLU live      = %d/%d (%.1f%%), negative %d"
          % (live, total, 100.0 * live / total, nneg))
    print("margin to kink = %.4f  (a pre within ~1e-6 of zero would make d_w1 a"
          % margin)
    print("                 function of summation order instead of of the maths)")
    print("at var floor   = %d/%d" % (at_floor, N * C))
    print("d_x absmax     = %.6f" % float(x.grad.abs().max()))
    print("d_x absmin     = %.6f  (near-zero coords make relative tests meaningless)"
          % float(x.grad.abs().min()))
    print("d_x span       = %.1fx"
          % (float(x.grad.abs().max()) / max(float(x.grad.abs().min()), 1e-30)))
    print("d_w1 absmax    = %.6f" % float(w1.grad.abs().max()))
    print("d_b1 absmax    = %.6f" % float(b1.grad.abs().max()))
    print("d_bn_gamma     = %s" % [round(float(v), 6) for v in gamma.grad])
    print("d_bn_beta      = %s" % [round(float(v), 6) for v in beta.grad])
    print("d_w2 absmax    = %.6f" % float(w2.grad.abs().max()))
    print("d_b2 absmax    = %.6f" % float(b2.grad.abs().max()))
    # A BN affine gradient that is all zeros would mean the oracle never
    # differentiated the affine, and the gate for it would pass vacuously.
    if float(gamma.grad.abs().max()) == 0.0 or float(beta.grad.abs().max()) == 0.0:
        sys.exit("the BatchNorm affine received no gradient; the gate for "
                 "d_att_bn_gamma/d_att_bn_beta would be vacuous")

    src = f'''// GENERATED by verify/asp_oracle.py from PyTorch {torch.__version__}.
// Do not hand-edit: the gate that reads these compares against them.
//
// The oracle is autograd, not finite differences. `tests/wave2/tarnet_test.mbt`
// already gates ASP against central differences at rtol 3e-2 and is green;
// this file pins the same gradients to float32 roundoff so the slack in that
// gate becomes visible instead of assumed.
//
// Fixture: n={N}, c={C}, t={T}, att_hidden={ATT}. ReLU is live on {live}/{total}
// pre-activations ({100.0 * live / total:.1f}%), so the context path -- the
// attention-to-input coupling that `tarnet_asp.mbt` records as unresolved --
// is actually exercised here rather than masked to zero.
//
// `b1` carries a searched offset of {offset:.2f}, chosen so the closest
// pre-activation sits {margin:.4f} from zero with {nneg} still negative. The
// search lives in `choose_offset`: the offset that maximises the margin with no
// sign constraint simply switches ReLU always on and tests less. A
// pre-activation near zero is a discontinuity -- one float32 ulp of
// disagreement between two summation orders flips a derivative, and every
// attention weight gradient with it.

///|
///|```text
///|ASP_N / ASP_C / ASP_T / ASP_ATT : oracle fixture shape
///|```

///|
///|```text
///|ASP_N
///|```

pub const ASP_N : Int = {N}

///|
///|```text
///|ASP_C
///|```

pub const ASP_C : Int = {C}

///|
///|```text
///|ASP_T
///|```

pub const ASP_T : Int = {T}

///|
///|```text
///|ASP_ATT
///|```

pub const ASP_ATT : Int = {ATT}

///|
///|```text
///|ASP_STD_FLOOR / ASP_VAR_FLOOR : the constants asp_forward uses
///|```

///|
///|```text
///|ASP_STD_FLOOR
///|```

pub const ASP_STD_FLOOR : Float = 0.0001F

///|
///|```text
///|ASP_VAR_FLOOR
///|```

pub const ASP_VAR_FLOOR : Float = 0.0001F

///|
///|```text
///|ASP_TOL_FWD : forward tolerance against the autograd oracle
///|```

///|
///|```text
///|ASP_TOL_FWD
///|```

///| Mixed absolute/relative, for the same reason as the BatchNorm1d gate:
///| `alpha` is a softmax over `t` and `mu` subtracts two products of similar
///| size, so the output's precision is set by its intermediates. Measured worst
///| absolute forward error against this oracle is reported by the gate itself.
///|
pub const ASP_TOL_FWD : Float = 1.0e-4F

///|
///|```text
///|ASP_TOL_GRAD : gradient tolerance against the autograd oracle
///|```

///|
///|```text
///|ASP_TOL_GRAD
///|```

///| d_x here spans {float(x.grad.abs().max()) / max(float(x.grad.abs().min()), 1e-30):.0f}x,
///| so a per-coordinate relative bound is meaningless on the small ones. The
///| absolute term is what carries the check; the mutants in the gate's own
///| sweep are far larger than it.
///|
pub const ASP_TOL_GRAD : Float = 1.0e-2F

///|
///|```text
///|ASP_ATOL_GRAD
///|```

///|
///|```text
///|ASP_ATOL_GRAD
///|```

pub const ASP_ATOL_GRAD : Float = 1.0e-3F

///|
///|```text
///|asp_x / asp_w1 / asp_b1 / asp_w2 / asp_b2 : oracle fixture
///|```

///|
///|```text
///|asp_x
///|```

pub let asp_x : Array[Float] = {fmt(xf)}

///|
///|```text
///|asp_w1
///|```

pub let asp_w1 : Array[Float] = {fmt(w1f)}

///|
///|```text
///|asp_b1
///|"""

///|
///|```text
///|asp_b1
///|"""

pub let asp_b1 : Array[Float] = {fmt(b1f)}

///|
///|```text
///|asp_w2
///|```

pub let asp_w2 : Array[Float] = {fmt(w2f)}

///|
///|```text
///|asp_b2
///|"""

///|
///|```text
///|asp_b2
///|"""

pub let asp_b2 : Array[Float] = {fmt(b2f)}

///|
///|```text
///|asp_bn_gamma : the ASP BatchNorm1d's per-unit affine, distinct on every unit
///|"""

///|
///|```text
///|asp_bn_gamma
///|"""

pub let asp_bn_gamma : Array[Float] = {fmt(bn_g)}

///|
///|```text
///|asp_bn_beta : likewise; never all-zero, so a swapped pair is visible
///|"""

///|
///|```text
///|asp_bn_beta
///|"""

pub let asp_bn_beta : Array[Float] = {fmt(bn_b)}

///|
///|```text
///|asp_g : fixed incoming d_out
///|"""

///|
///|```text
///|asp_g
///|"""

pub let asp_g : Array[Float] = {fmt(gf)}

///|
///|```text
///|asp_out_golden : asp_forward under torch autograd
///|"""

///|
///|```text
///|asp_out_golden
///|"""

pub let asp_out_golden : Array[Float] = {fmt(out.detach().numpy())}

///|
///|```text
///|asp_dx_golden : d(sum(out*g))/d x
///|"""

///|
///|```text
///|asp_dx_golden
///|"""

pub let asp_dx_golden : Array[Float] = {fmt(x.grad.numpy())}

///|
///|```text
///|asp_dw1_golden
///|"""

///|
///|```text
///|asp_dw1_golden
///|"""

pub let asp_dw1_golden : Array[Float] = {fmt(w1.grad.numpy())}

///|
///|```text
///|asp_db1_golden
///|"""

///|
///|```text
///|asp_db1_golden
///|"""

pub let asp_db1_golden : Array[Float] = {fmt(b1.grad.numpy())}

///|
///|```text
///|asp_dbn_gamma_golden : d loss / d (BatchNorm1d gamma), per attention unit
///|"""

///|
///|```text
///|asp_dbn_gamma_golden
///|"""

pub let asp_dbn_gamma_golden : Array[Float] = {fmt(gamma.grad.numpy())}

///|
///|```text
///|asp_dbn_beta_golden
///|"""

///|
///|```text
///|asp_dbn_beta_golden
///|"""

pub let asp_dbn_beta_golden : Array[Float] = {fmt(beta.grad.numpy())}

///|
///|```text
///|asp_dw2_golden
///|"""

///|
///|```text
///|asp_dw2_golden
///|"""

pub let asp_dw2_golden : Array[Float] = {fmt(w2.grad.numpy())}

///|
///|```text
///|asp_db2_golden
///|"""

///|
///|```text
///|asp_db2_golden
///|"""

pub let asp_db2_golden : Array[Float] = {fmt(b2.grad.numpy())}

///|
///|```text
///|ASP_RELU_LIVE : pre-activations above zero, out of ASP_ATT*n*c*t
///|"""

///|
///|```text
///|ASP_RELU_LIVE
///|"""

pub const ASP_RELU_LIVE : Int = {live}

///|
///|```text
///|ASP_RELU_MIN : distance from the closest pre-activation to ReLU's kink
///|```

///|
///|```text
///|ASP_RELU_MIN
///|```

pub const ASP_RELU_MIN : Float = {margin:.6f}F
'''

    out_path = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "mbt", "tests", "wave2", "asp_oracle_golden.mbt",
    )
    with io.open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(src)
    print("wrote %s" % out_path)


if __name__ == "__main__":
    main()
