# bn1d_backward_probe.py -- derive the gradient gate's absolute tolerance.
#
# G2 compares the analytic d_x against the PyTorch autograd golden with a pure
# relative tolerance, and 3 of 168 coordinates fall outside 1e-3. Before
# widening that, find out whether the residue is float32 arithmetic or a bug.
#
# The route: emulate `batch_norm2d_backward` term for term in float32, the way
# the MoonBit code evaluates it, and compare against the golden. Then derive
# the bound from the arithmetic instead of fitting it:
#
#   d_input = dxhat*inv + d_var*2*xc/nhw + d_mean/nhw
#
# The three terms can cancel, so the error is set by the LARGEST of them, not
# by the result. atol = 2 * FLT_EPS/2 * max|term| * sqrt(n*w).
#
# Run:  python verify/bn1d_backward_probe.py

import os
import sys

import numpy as np
import torch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from bn1d_tolerance_probe import GAMMA, BETA  # noqa: E402
from gen_bn1d_golden import C, EPS, N, W, fixture  # noqa: E402

FLT_EPS = np.float32(1.1920929e-07)


def main():
    x = fixture()
    xf = x.numpy().astype(np.float32).reshape(N, C, W)

    # torch goldens
    xg = x.clone().requires_grad_(True)
    gg = torch.tensor(GAMMA, requires_grad=True)
    bg = torch.tensor(BETA, requires_grad=True)
    yg = torch.nn.functional.batch_norm(xg, None, None, gg, bg, True, 0.0, EPS)
    wts = torch.tensor([1.0, 2.0, 3.0, 4.0]).view(N, 1, 1)
    (yg * yg * wts).sum().backward()
    golden = xg.grad.detach().reshape(-1).numpy().astype(np.float32)

    # forward cache, exactly as MoonBit computes it (two-pass, sequential)
    mean = np.zeros(C, dtype=np.float32)
    inv_std = np.zeros(C, dtype=np.float32)
    x_centered = np.zeros((N, C, W), dtype=np.float32)
    for ci in range(C):
        s = np.float32(0.0)
        for ni in range(N):
            for wi in range(W):
                s = np.float32(s + xf[ni, ci, wi])
        m = np.float32(s / np.float32(N * W))
        sq = np.float32(0.0)
        for ni in range(N):
            for wi in range(W):
                d = np.float32(xf[ni, ci, wi] - m)
                sq = np.float32(sq + np.float32(d * d))
        v = np.float32(sq / np.float32(N * W))
        mean[ci] = m
        inv_std[ci] = np.float32(1.0) / np.float32(
            np.sqrt(np.float32(v + np.float32(EPS)))
        )
        for ni in range(N):
            for wi in range(W):
                x_centered[ni, ci, wi] = np.float32(xf[ni, ci, wi] - m)

    # d_output
    y = torch.nn.functional.batch_norm(
        torch.tensor(xf), None, None,
        torch.tensor(GAMMA), torch.tensor(BETA), True, 0.0, EPS
    ).numpy().astype(np.float32)
    dy = np.float32(2.0) * y * wts.numpy().astype(np.float32)

    # backward, term for term as batch_norm2d_backward does it
    dx = np.zeros((N, C, W), dtype=np.float32)
    max_term = 0.0
    nhw_f = np.float32(N * W)
    for ci in range(C):
        inv = inv_std[ci]
        sum_dxhat = np.float32(0.0)
        sum_dxhat_xc = np.float32(0.0)
        for ni in range(N):
            for wi in range(W):
                dyv = dy[ni, ci, wi]
                dxhat = np.float32(dyv * np.float32(GAMMA[ci]))
                sum_dxhat = np.float32(sum_dxhat + dxhat)
                sum_dxhat_xc = np.float32(
                    sum_dxhat_xc + np.float32(dxhat * x_centered[ni, ci, wi])
                )
        d_var = np.float32(-0.5 * np.float32(sum_dxhat_xc
                                            * np.float32(inv * inv * inv)))
        d_mean = np.float32(-np.float32(sum_dxhat * inv))
        inv_nhw = np.float32(1.0) / nhw_f
        for ni in range(N):
            for wi in range(W):
                dxhat = np.float32(dy[ni, ci, wi] * np.float32(GAMMA[ci]))
                xc = x_centered[ni, ci, wi]
                t1 = np.float32(dxhat * inv)
                t2 = np.float32(np.float32(d_var * 2.0) * np.float32(xc * inv_nhw))
                t3 = np.float32(d_mean * inv_nhw)
                dx[ni, ci, wi] = np.float32(np.float32(t1 + t2) + t3)
                mag = abs(float(t1)) + abs(float(t2)) + abs(float(t3))
                if mag > max_term:
                    max_term = mag

    dxf = dx.reshape(-1)
    diff = np.abs(dxf - golden)
    rel = diff / np.maximum(np.abs(golden), 1e-30)
    scale = float(np.abs(golden).max())

    print("golden |d_x| min %.3e  max %.3e" % (np.abs(golden).min(), scale))
    print("largest backward term |t1|+|t2|+|t3| = %.4f" % max_term)
    print()
    print("worst ABSOLUTE error over 168 coords : %.4e" % diff.max())
    print("worst error as a fraction of scale   : %.4e" % (diff.max() / scale))
    print()
    order = np.argsort(-rel)[:6]
    print("worst 6 by PURE RELATIVE:")
    print("   idx      golden        ours         abs err     rel err")
    for i in order:
        print("  %4d  %11.4e  %11.4e  %10.3e  %9.3e"
              % (i, golden[i], dxf[i], diff[i], rel[i]))
    print()
    atol = 2.0 * (FLT_EPS / 2) * max_term * (N * W) ** 0.5
    print("derived atol = 2*(FLT_EPS/2)*max_term*sqrt(n*w) = %.4e" % atol)
    print("measured worst absolute error                = %.4e" % diff.max())
    print("margin: %.1fx" % (atol / max(diff.max(), 1e-30)))
    print()
    for rtol in [1e-3, 1e-4]:
        bad = int(np.sum(diff > atol + rtol * np.abs(golden)))
        print("atol=%.3e rtol=%.0e -> %3d/168 outside" % (atol, rtol, bad))


if __name__ == "__main__":
    main()
