# bn1d_tolerance_probe.py -- why do 12 of 168 forward coordinates sit at 2.5e-3?
#
# After switching batch_norm2d_forward to the two-pass variance, G1's worst
# forward error fell from 7.6e-2 to 2.5e-3, but 12 coordinates still sit outside
# a 1e-4 relative tolerance. Two candidate explanations:
#
#   (a) the MoonBit forward is still wrong somewhere, or
#   (b) the coordinates that fail are the ones whose golden value is near zero,
#       where a pure RELATIVE metric is meaningless. `y = gamma*d*inv + beta`
#       crosses zero for most channels, so a value that should read 1e-5 is
#       compared against a tolerance of 1e-4 * 1e-5.
#
# This emulates the MoonBit forward exactly in float32 -- sequential summation,
# no pairwise trickery -- and reports both metrics so the distinction is a
# measurement rather than an argument.
#
# Run:  python verify/bn1d_tolerance_probe.py

import os
import sys

import numpy as np
import torch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gen_bn1d_golden import C, EPS, N, W, fixture  # noqa: E402

GAMMA = np.array([1.0, 0.8, 1.2, 0.9, 1.1, 0.7], dtype=np.float32)
BETA = np.array([0.1, -0.2, 0.05, 0.0, 0.3, -0.1], dtype=np.float32)


def moonbit_forward(x):
    """The two-pass float32 forward, summed sequentially like MoonBit does.

    Layout is [n, c, w]: channel stride W, sample stride c*w.
    """
    f32 = np.float32
    x = x.numpy().astype(np.float32).reshape(N, C, W)
    out = np.zeros_like(x)
    flat = x.reshape(-1)
    oflat = out.reshape(-1)
    for ci in range(C):
        s = f32(0.0)
        for ni in range(N):
            for wi in range(W):
                s = f32(s + flat[ni * C * W + ci * W + wi])
        m = f32(s / f32(N * W))
        sq = f32(0.0)
        for ni in range(N):
            for wi in range(W):
                d = f32(flat[ni * C * W + ci * W + wi] - m)
                sq = f32(sq + f32(d * d))
        v = f32(sq / f32(N * W))
        inv = f32(1.0) / np.float32(np.sqrt(f32(v + f32(EPS))))
        for ni in range(N):
            for wi in range(W):
                i = ni * C * W + ci * W + wi
                oflat[i] = f32(
                    f32(f32(GAMMA[ci] * f32(flat[i] - m)) * inv) + BETA[ci]
                )
    return out


def main():
    x = fixture()
    mine = moonbit_forward(x)

    bn = torch.nn.BatchNorm1d(C, eps=EPS)
    with torch.no_grad():
        bn.weight.copy_(torch.tensor(GAMMA))
        bn.bias.copy_(torch.tensor(BETA))
    bn.train()
    gold = bn(x).detach().numpy()

    diff = np.abs(mine - gold)
    rel = diff / np.maximum(np.abs(gold), np.float32(1e-6))

    # (b) hypothesis: the failures sit where the golden value is near zero.
    order = np.argsort(-rel.reshape(-1))[:12]
    print("worst 12 coordinates by PURE RELATIVE error:")
    print("   idx   golden        mine          abs err     rel err")
    for i in order:
        print("  %4d  %11.3e  %11.3e  %10.3e  %9.3e"
              % (i, gold.reshape(-1)[i], mine.reshape(-1)[i],
                 diff.reshape(-1)[i], rel.reshape(-1)[i]))
    print()
    print("golden magnitude at those 12: min %.3e  max %.3e"
          % (min(abs(gold.reshape(-1)[i]) for i in order),
             max(abs(gold.reshape(-1)[i]) for i in order)))
    print("|y| over the whole tensor:    min %.3e  max %.3e"
          % (np.abs(gold).min(), np.abs(gold).max()))
    print()
    print("worst ABSOLUTE error over all 168 coords: %.3e" % diff.max())
    print("worst relative error where |golden| > 0.1:   %.3e"
          % rel[np.abs(gold) > 0.1].max())
    print()
    # The honest criterion for a value built by cancelling two ~7.5 terms:
    # a mixed absolute/relative bound.
    for atol, rtol in [(1e-3, 1e-4), (2e-3, 1e-4), (1e-3, 1e-5), (5e-3, 1e-4)]:
        bad = int(np.sum(diff > atol + rtol * np.abs(gold)))
        print("atol=%-8.0e rtol=%-8.0e -> %3d/168 outside" % (atol, rtol, bad))
    print()
    print("If a mixed bound clears the run while pure relative does not, the")
    print("residue is the metric, not the forward.")


if __name__ == "__main__":
    main()
