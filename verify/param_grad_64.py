# param_grad_64.py -- is the parameter-gradient tail float32, or a real defect?
#
# The block oracle agrees with MoonBit to 4.5e-05 on the forward and on d_x, but
# 42 of 449 parameter gradients per block sit outside that bound, scattered
# across pw1.weight, pw1.bias and dw.weight rather than clustered in one array.
# A wrong formula breaks a contiguous array; scattered disagreements look like
# conditioning. "Looks like" is not a measurement.
#
# So: recompute the same oracle in float64 and compare it to its own float32
# result. If torch-float32 disagrees with torch-float64 at the SAME coordinates
# by a similar amount, then torch's float32 gradient is the noisy one and
# MoonBit is fine. If float64 and float32 agree with each other and both
# disagree with MoonBit at the same coordinates, MoonBit is wrong there and the
# tolerance is not the problem.
#
# Run:  python verify/param_grad_64.py <block_export.log>

import os
import sys

import numpy as np
import torch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gen_block_oracle import (BLOCK_PARAMS, EPS, HEADER, Block,  # noqa: E402
                              flat_param_grad, parse)

N, C, T, HID, KERNEL, C_RED = 3, 8, 6, 8, 3, 4
DIALS = [1, 2, 4, 8, 16]


def fixture(seed, dtype):
    x = np.zeros((N, C, T))
    for b in range(N):
        for c in range(C):
            base = 0.5 * c - 2.0
            scale = 0.3 + 0.2 * c
            for ti in range(T):
                h = ((b * T + ti) * 37 + c * 101 + seed) % 71 - 35
                x[b, c, ti] = base + scale * h / 35.0
    return torch.tensor(x, dtype=dtype)


def build(params, i, dtype):
    b = Block(C, HID, KERNEL, DIALS[i], C_RED,
              params[HEADER + i * BLOCK_PARAMS:
                     HEADER + (i + 1) * BLOCK_PARAMS])
    return b.to(dtype)


def main():
    params = np.array(parse(sys.argv[1])["params"], dtype=np.float64)
    d64 = fixture(500, torch.float64)

    rows = []
    for i in range(len(DIALS)):
        p32 = params.astype(np.float32)
        x32 = fixture(0, torch.float32).requires_grad_(True)
        b32 = build(p32, i, torch.float32)
        (b32(x32) * fixture(500, torch.float32)).sum().backward()
        g32 = flat_param_grad(b32)

        x64 = fixture(0, torch.float64).requires_grad_(True)
        b64 = build(params, i, torch.float64)
        (b64(x64) * d64).sum().backward()
        g64 = _collect(b64)

        scale = float(np.abs(g32).max())
        d_ff = float(np.abs(g32.astype(np.float64) - g64).max())
        rows.append((i, DIALS[i], scale, d_ff, d_ff / scale))

    print("block  dil   |d_param| scale   f32-vs-f64 max|d|   relative")
    print("-----------------------------------------------------------")
    for i, dil, scale, d, r in rows:
        print("  %d   %2d   %12.4f   %18.4e   %8.2e" % (i, dil, scale, d, r))
    print()
    print("If that relative figure is the same order as the 3.3e-04 the gate")
    print("reported against MoonBit, then torch's OWN float32 gradient is the")
    print("noisy side and the bound belongs on the arithmetic, not on MoonBit.")


def _collect(module):
    """Parameter gradients in the same order `flat_param_grad` walks."""
    out = []
    out += list(module.pw1.weight.grad.reshape(-1))
    out += list(module.pw1.bias.grad)
    out += list(module.ln1.gamma.grad)
    out += list(module.ln1.beta.grad)
    out += list(module.dw.weight.grad.reshape(-1))
    out += list(module.dw.bias.grad)
    out += list(module.prelu.grad.reshape(-1))
    out += list(module.ln2.gamma.grad)
    out += list(module.ln2.beta.grad)
    out += list(module.pw2.weight.grad.reshape(-1))
    out += list(module.pw2.bias.grad)
    for m in module.ctse.conv:
        out += list(m.weight.grad.reshape(-1))
        out += list(m.bias.grad)
    out += list(module.ctse.feature_fc.weight.grad.reshape(-1))
    out += list(module.ctse.feature_fc.bias.grad)
    out += list(module.ctse.fc1.weight.grad.reshape(-1))
    out += list(module.ctse.fc1.bias.grad)
    out += list(module.ctse.fc2.weight.grad.reshape(-1))
    out += list(module.ctse.fc2.bias.grad)
    return np.concatenate([np.asarray(v).reshape(-1) for v in out]).astype(np.float64)


if __name__ == "__main__":
    main()
