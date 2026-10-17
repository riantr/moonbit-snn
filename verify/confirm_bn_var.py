# confirm_bn_var.py -- is the BatchNorm2d variance formula the culprit?
#
# G1a reported a 2% relative error on inv_std while the per-channel mean was
# exact to 1e-5. That points at the variance, and specifically at the
# "naive" one-pass form E[x^2] - E[x]^2 that batch_norm2d_forward uses.
#
# The claim to check: for a channel whose mean is far from zero and whose
# variance is small, E[x^2] - E[x]^2 cancels catastrophically in float32.
#
# Run:  python verify/confirm_bn_var.py

import os
import sys

import torch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gen_bn1d_golden import C, EPS, N, W, fixture  # noqa: E402

EPS64 = 1e-5


def main():
    x = fixture()
    x64 = x.double()

    mean64 = x64.mean(dim=(0, 2))
    var64 = x64.var(dim=(0, 2), unbiased=False)

    # (a) float64, two-pass: E[(x - mean)^2]  -- what PyTorch computes
    two_pass = (x64 - mean64.view(1, C, 1)) ** 2
    two_pass = two_pass.mean(dim=(0, 2))

    # (b) float64, one-pass: E[x^2] - E[x]^2  -- mathematically identical
    one_pass64 = (x64 ** 2).mean(dim=(0, 2)) - mean64 ** 2

    # (c) float32, one-pass: what batch_norm2d_forward actually computes
    x32 = x
    sum32 = x32.sum(dim=(0, 2))
    sq32 = (x32 * x32).sum(dim=(0, 2))
    cnt = float(N * W)
    one_pass32 = sq32 / cnt - (sum32 / cnt) ** 2

    print("per-channel:  var(true)     E[x2]-E[x]2 (f64)   E[x2]-E[x]2 (f32)   rel err (f32)")
    for c in range(C):
        rel = abs(float(one_pass32[c]) - float(var64[c])) / float(var64[c])
        print(
            "  ch%d  %14.6e %14.6e %14.6e        %8.4f%%   inv_std err %7.4f%%"
            % (
                c,
                float(var64[c]),
                float(one_pass64[c]),
                float(one_pass32[c]),
                100.0 * rel,
                100.0 * abs(
                    1.0 / (float(one_pass32[c]) + EPS) ** 0.5
                    - 1.0 / (float(var64[c]) + EPS64) ** 0.5
                ) / (1.0 / (float(var64[c]) + EPS64) ** 0.5),
            )
        )

    print()
    print("float64 one-pass vs two-pass (mathematically identical, float64 here):")
    for c in range(C):
        rel = abs(float(one_pass64[c]) - float(two_pass[c])) / float(two_pass[c])
        print("  ch%d  rel %8.3e" % (c, rel))
    print()
    print("Conclusion: if ch0's f32 rel err is large while its f64 rel err is")
    print("~1e-16, the formula is the defect, not the fixture.")


if __name__ == "__main__":
    main()
