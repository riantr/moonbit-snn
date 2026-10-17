# bn1d_fd_probe.py -- how accurate is a finite-difference gradient here, really?
#
# G3 compares, for each mutant, the gradient a finite-difference gate would see
# against the PyTorch autograd golden. That comparison only means something if
# the finite difference itself is accurate enough. Run at h = 1e-3 on this
# float32 forward it reported 105 of 168 coordinates outside a 1e-3 relative
# tolerance for the CORRECT implementation -- i.e. the instrument is noisier
# than the thing it is meant to measure.
#
# So: measure the noise floor, then the smallest mutant effect, then pick the
# FD tolerance in between with room on both sides. Anything else is guessing.
#
# Run:  python verify/bn1d_fd_probe.py

import os
import sys

import numpy as np
import torch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from bn1d_tolerance_probe import GAMMA, BETA, moonbit_forward  # noqa: E402
from gen_bn1d_golden import C, EPS, N, W, fixture  # noqa: E402

MUTANTS = ["correct", "no_eps", "no_mean_sub", "no_sqrt", "no_gamma",
           "no_beta", "swap_ab", "per_sample", "unbiased"]


def fwd(x, mutant):
    """float32 two-pass BatchNorm1d with one deliberate flaw, numpy emulated."""
    f32 = np.float32
    x = x.astype(np.float32).reshape(N, C, W)
    out = np.zeros_like(x)
    for ci in range(C):
        if mutant == "per_sample":
            for ni in range(N):
                xs = x[ni, ci, :]
                cnt = f32(W)
                m = xs.sum(dtype=np.float32) / cnt
                v = ((xs - m) ** 2).sum(dtype=np.float32) / cnt
                out[ni, ci, :] = _affine(mutant, ci, xs - m, v)
        else:
            xs = x[:, ci, :]
            cnt = f32(N * W)
            m = xs.sum(dtype=np.float32) / cnt
            dev = (xs - m).astype(np.float32)
            if mutant == "no_mean_sub":
                v = (xs.astype(np.float32) ** 2).sum(dtype=np.float32) / cnt
            else:
                v = (dev ** 2).sum(dtype=np.float32) / cnt
            if mutant == "unbiased":
                v = f32(v * f32(cnt / f32(cnt - f32(1.0))))
            out[:, ci, :] = _affine(mutant, ci, dev, v)
    return out


def _affine(mutant, ci, dev, v):
    f32 = np.float32
    if mutant == "no_eps":
        e = f32(0.0)
    else:
        e = f32(EPS)
    inv = f32(1.0) if mutant == "no_sqrt" else f32(1.0) / np.float32(
        np.sqrt(f32(v + e))
    )
    if mutant == "no_gamma":
        g, b = f32(1.0), BETA[ci]
    elif mutant == "no_beta":
        g, b = GAMMA[ci], f32(0.0)
    elif mutant == "swap_ab":
        g, b = BETA[ci], GAMMA[ci]
    else:
        g, b = GAMMA[ci], BETA[ci]
    return (f32(f32(g * dev) * inv) + b).astype(np.float32)


def loss(y):
    wts = np.array([1.0, 2.0, 3.0, 4.0], dtype=np.float32).reshape(N, 1, 1)
    return float((y.astype(np.float32) ** 2 * wts).sum(dtype=np.float32))


def fd_grad(x, mutant, h):
    base = x.astype(np.float32).reshape(-1).copy()
    out = np.zeros_like(base)
    for i in range(base.size):
        up = base.copy()
        dn = base.copy()
        up[i] = np.float32(up[i] + np.float32(h))
        dn[i] = np.float32(dn[i] - np.float32(h))
        out[i] = (loss(fwd(up, mutant)) - loss(fwd(dn, mutant))) / (2.0 * h)
    return out


def main():
    x = fixture()
    xf = x.numpy().astype(np.float32)

    xg = x.clone().requires_grad_(True)
    gg = torch.tensor(GAMMA, requires_grad=True)
    bg = torch.tensor(BETA, requires_grad=True)
    yg = torch.nn.functional.batch_norm(xg, None, None, gg, bg, True, 0.0, EPS)
    wts = torch.tensor([1.0, 2.0, 3.0, 4.0]).view(N, 1, 1)
    (yg * yg * wts).sum().backward()
    golden = xg.grad.reshape(-1).numpy()
    gmax = float(np.abs(golden).max())
    gmin = float(np.abs(golden).min())
    print("golden |d_x|: min %.3e  max %.3e  (span %.0fx)"
          % (gmin, gmax, gmax / gmin))
    print()

    print("FD noise on the CORRECT implementation, by step size:")
    print("  rel error measured as |FD-golden| / |golden|")
    best = None
    for h in [1e-2, 3e-3, 1e-3, 3e-4, 1e-4]:
        g = fd_grad(xf, "correct", h)
        rel = np.abs(g - golden) / np.maximum(np.abs(golden), 1e-9)
        for tol in [1e-2, 5e-3, 2e-3, 1e-3, 5e-4, 1e-4]:
            pass
        counts = {tol: int(np.sum(rel > tol)) for tol in [1e-2, 2e-3, 1e-3, 5e-4]}
        print("  h=%-7g worst rel %.3e   out-of-tol %s"
              % (h, rel.max(), counts))
        if best is None or rel.max() < best[1]:
            best = (h, rel.max())
    print()
    print("best step %g, worst relative error %.3e" % best)
    print()

    # At the best step, how many coordinates does FD miss for each mutant?
    h = best[0]
    print("at h=%g, coordinates where FD is INSIDE tolerance but the mutant" % h)
    print("is genuinely different from the correct gradient:")
    for m in MUTANTS:
        g = fd_grad(xf, m, h)
        rel = np.abs(g - golden) / np.maximum(np.abs(golden), 1e-9)
        # how far is this mutant's FD from the CORRECT implementation's FD?
        gc = fd_grad(xf, "correct", h)
        relc = np.abs(g - gc) / np.maximum(np.abs(gc), 1e-9)
        print("  %-13s FD-vs-golden worst %.3e   FD-vs-correctFD worst %.3e"
              % (m, rel.max(), relc.max()))
    print()
    print("Reading: for a mutant to be MISSED by an FD gate, |FD_mutant - golden|")
    print("must sit under the FD tolerance, which is set by the noise above.")


if __name__ == "__main__":
    main()
