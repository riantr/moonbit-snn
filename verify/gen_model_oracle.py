# gen_model_oracle.py -- a PyTorch autograd oracle for the WHOLE TARNet.
#
# WHY, when every layer already has one. The block and its CTSE, the ASP head and
# its BatchNorm1d, and the post-MFA SE are each verified against an independent
# reference. The WIRING between them was not, and that is a gap of a
# particular kind: a mistake in the wiring leaves the forward wrong and the
# backward wrong in exactly the matching way, every finite difference agrees
# with both, and the model still trains -- on a network that is not the paper's.
#
# It is not theoretical. The first run of this gate failed on 12 of 12 logits
# and that is how the bottleneck's mis-laid-out 1x1 convolution was found.
#
# Composition, from `_build/tarnet_ref/tarnet/models/tarnet.py`:
#
#     layer_norm(in_chan) -> Conv1d(in_chan, bn_chan, 1)          [ln_in, bn_w/b]
#     three TCN stacks, dilations [1,2] [4,8] [16]               [5 blocks here]
#     concat along channels -> Conv1d(3*bn_chan, fuse_out, 1)
#     ReLU -> SEModule                                       [mfa_w/b, mfa_se]
#     ASPHead with BatchNorm1d(att_hidden)                   [asp.*]
#     BatchNorm1d(2*fuse_out) -> Linear(fuse_out, emb_dim)   [bn_seg, fc]
#     BatchNorm1d(emb_dim) -> Linear(emb_dim, n_classes)    [bn_out, cls]
#
# PARAMETERS come from a MoonBit-side export, because Tarnet::new draws from
# core's Xoshiro and only MoonBit can produce them. That is a real fragility:
# the goldens are committed but their input is not, and regenerating them needs
# the export step. `tests/wave2/model_export_test.mbt` provides it.
#
# Run:  python verify/gen_model_oracle.py <export.log>

import io
import os
import sys

import numpy as np
import torch
import torch.nn.functional as F

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from asp_oracle import asp_forward_torch  # noqa: E402
from gen_block_oracle import EPS, Block, flat_param_grad  # noqa: E402
from gen_se_oracle import SE  # noqa: E402

N, IN_CHAN, T = 3, 6, 6
BN_CHAN, FUSE, EMB, ATT, CLASSES = 8, 8, 8, 8, 4
BLOCK_PARAMS = 449
DILS = [1, 2, 4, 8, 16]
SE_BNK = 32
HEADER = 68                                   # ln_in.g+beta, bn_w, bn_b
BLOCKS_END = HEADER + 5 * BLOCK_PARAMS       # 2313
MFA_W_OFF = BLOCKS_END
MFA_B_OFF = MFA_W_OFF + FUSE * 3 * BN_CHAN   # 2505
SE_OFFSET = MFA_B_OFF + FUSE                 # 2513
SE_LEN = SE_BNK * FUSE + SE_BNK + FUSE * SE_BNK + FUSE
ASP_OFFSET = SE_OFFSET + SE_LEN             # 3065
TOTAL = 3573


def parse_params(path):
    """Read the flat parameter vector out of a MoonBit export.

    The encoding is detected rather than assumed: the export comes from
    PowerShell's `*> log`, which writes UTF-16LE, and reading that as UTF-8
    leaves a NUL between every character so every search silently misses.
    """
    raw = open(path, "rb").read()
    txt = None
    for enc in ("utf-8", "utf-16", "utf-16-le", "latin-1"):
        cand = raw.decode(enc, errors="replace")
        if "TNORACLE params " in cand:
            txt = cand
            break
    if txt is None:
        sys.exit("no TNORACLE marker in the export under any encoding")
    i = txt.find("TNORACLE params ")
    line = txt[i:txt.index("\n", i)]
    try:
        return np.array([float(v) for v in line.split()[2:]], dtype=np.float32)
    except ValueError as e:
        sys.exit("could not parse params: %s" % e)


def fixture(seed, c, t=T):
    """The integer-hash construction `model_oracle_test.mbt` reproduces."""
    x = np.zeros((N, c, T), dtype=np.float32)
    for b in range(N):
        for ch in range(c):
            base = 0.5 * ch - 2.0
            scale = 0.3 + 0.2 * ch
            for ti in range(T):
                # Hashes with `t`, the SAME `t` being looped -- the MoonBit side
                # hashes with the frame count it was given. Using the module
                # constant here instead silently generated DIFFERENT values
                # whenever a caller passed a shorter frame count, which is what
                # happened when the logit gradient became a single frame.
                h = ((b * t + ti) * 37 + ch * 101 + seed) % 71 - 35
                x[b, ch, ti] = base + scale * h / 35.0
    return torch.tensor(x)


class GLN(torch.nn.Module):
    """Global layer norm over the whole (C, T) plane of each sample."""

    def __init__(self, gamma, beta):
        super().__init__()
        self.gamma = torch.nn.Parameter(torch.tensor(gamma, dtype=torch.float32))
        self.beta = torch.nn.Parameter(torch.tensor(beta, dtype=torch.float32))

    def forward(self, x):
        mean = x.mean(dim=(1, 2), keepdim=True)
        var = (x - mean).pow(2).mean(dim=(1, 2), keepdim=True)
        return (self.gamma.view(1, -1, 1) * (x - mean)
                / torch.sqrt(var + EPS) + self.beta.view(1, -1, 1))


def conv1x1(x, w, b, out_c):
    """A 1x1 convolution: contract over the CHANNEL axis, frame by frame.

    Written this way deliberately. `nn.Linear`-style indexing over a `[n, c, t]`
    buffer contracts over a run of consecutive flat elements instead, which is
    the bug this oracle was built to find.
    """
    return F.conv1d(x, w.view(out_c, -1, 1), b)


def fmt(vals, per_line=6):
    def lit(v):
        s = repr(float(v))
        if "." not in s and "e" not in s and "E" not in s:
            s += ".0"
        return s + "F"
    flat = list(np.asarray(vals).reshape(-1))
    out, line = [], []
    for i, v in enumerate(flat):
        line.append(lit(v))
        if len(line) == per_line or i == len(flat) - 1:
            out.append("  " + ", ".join(line))
            line = []
    return "[\n" + ",\n".join(out) + ",\n]"


def main():
    p = parse_params(sys.argv[1])
    assert len(p) == TOTAL, "expected %d parameters, got %d" % (TOTAL, len(p))

    k = 0

    def take(n):
        nonlocal k
        v = p[k:k + n].copy()
        k += n
        return v

    def P(n):
        par = torch.nn.Parameter(torch.tensor(take(n), dtype=torch.float32))
        return par

    x = fixture(0, IN_CHAN).requires_grad_(True)
    ln_in = GLN(P(IN_CHAN), P(IN_CHAN))
    bn_w, bn_b = P(FUSE * IN_CHAN), P(FUSE)
    assert k == HEADER

    h = conv1x1(ln_in(x), bn_w, bn_b, BN_CHAN)

    # Three PARALLEL stacks off the bottleneck, each a SEQUENCE of blocks, and
    # it is the last block of each that concatenates -- not all five blocks.
    blks = [Block(BN_CHAN, BN_CHAN, 3, dil, BN_CHAN // 2,
                  p[HEADER + i * BLOCK_PARAMS:HEADER + (i + 1) * BLOCK_PARAMS]
                  .copy())
            for i, dil in enumerate(DILS)]
    s = blks[1](blks[0](h))     # stack_s: dilations [1, 2]
    m = blks[3](blks[2](h))     # stack_m: dilations [4, 8]
    l = blks[4](h)              # stack_l: dilation  [16]

    cat = torch.cat([s, m, l], dim=1)
    assert cat.shape[1] == 3 * BN_CHAN, \
        "concat gave %d channels, the MFA expects %d" % (cat.shape[1], 3 * BN_CHAN)

    # The blocks were read by direct slice, so skip over them.
    k = MFA_W_OFF
    mfa_w, mfa_b = P(FUSE * 3 * BN_CHAN), P(FUSE)
    mfa = F.relu(conv1x1(cat, mfa_w, mfa_b, FUSE))
    assert k == SE_OFFSET, "MFA walk ended at %d, expected %d" % (k, SE_OFFSET)

    se = SE(FUSE, SE_BNK, p[SE_OFFSET:SE_OFFSET + SE_LEN].copy())
    mfa = se(mfa)
    # Likewise for the SE: without this jump the ASP parameters below would be
    # read out of the SE's slice -- right shapes, wrong values, no error.
    k = ASP_OFFSET

    # Keep the Parameters themselves and hand VIEWS to the forward. A
    # `.view()` of a leaf is not a leaf, so `.grad` on the view is never
    # populated -- the ASP gradients came out as 34 scalars instead of 288.
    asp_w1_p = P(ATT * 3 * FUSE)
    asp_b1_p, asp_bn_g_p, asp_bn_b_p = P(ATT), P(ATT), P(ATT)
    asp_w2_p = P(FUSE * ATT)
    asp_b2_p = P(FUSE)
    pooled, _alpha, _ctx = asp_forward_torch(
        mfa,
        asp_w1_p.view(ATT, 3 * FUSE), asp_b1_p, asp_bn_g_p, asp_bn_b_p,
        asp_w2_p.view(FUSE, ATT), asp_b2_p,
    )

    seg_g, seg_b = P(2 * FUSE), P(2 * FUSE)
    seg = F.batch_norm(pooled.unsqueeze(2), None, None, seg_g, seg_b,
                       True, 0.0, 1e-5)
    fc_w, fc_b = P(EMB * 2 * FUSE), P(EMB)
    e = conv1x1(seg, fc_w, fc_b, EMB)
    out_g, out_b = P(EMB), P(EMB)
    emb = F.batch_norm(e, None, None, out_g, out_b, True, 0.0, 1e-5)
    cls_w, cls_b = P(CLASSES * EMB), P(CLASSES)
    logits = conv1x1(emb, cls_w, cls_b, CLASSES)

    assert k == TOTAL, "walk ended at %d, expected %d" % (k, TOTAL)

    # The logit gradient is [n, n_classes, 1] -- ONE frame, because the ASP head
    # pools over time and the heads see a single frame per sample. Passing a
    # [n, n_classes, t] tensor here would be a different gradient entirely, and
    # every parameter gradient downstream of it would then disagree for reasons
    # that have nothing to do with the model.
    d_logits = fixture(500, CLASSES, 1)
    (logits * d_logits).sum().backward()

    dp = []
    def add(label, values):
        n = sum(int(np.asarray(v).reshape(-1).size) for v in values)
        print("  %-28s %5d" % (label, n))
        dp.extend(np.asarray(v).reshape(-1).tolist() for v in [np.asarray(v).reshape(-1) for v in values])

    add("ln_in", [ln_in.gamma.grad, ln_in.beta.grad])
    add("bn_w/b", [bn_w.grad.reshape(-1), bn_b.grad])
    for i, blk in enumerate(blks):
        add("block %d" % i, [flat_param_grad(blk)])
    add("mfa", [mfa_w.grad.reshape(-1), mfa_b.grad])
    add("mfa_se", [se.fc1.weight.grad.reshape(-1), se.fc1.bias.grad,
                   se.fc2.weight.grad.reshape(-1), se.fc2.bias.grad])
    add("asp", [asp_w1_p.grad.reshape(-1), asp_b1_p.grad,
                asp_bn_g_p.grad, asp_bn_b_p.grad,
                asp_w2_p.grad.reshape(-1), asp_b2_p.grad])
    add("bn_seg", [seg_g.grad, seg_b.grad])
    add("fc", [fc_w.grad.reshape(-1), fc_b.grad])
    add("bn_out", [out_g.grad, out_b.grad])
    add("cls", [cls_w.grad.reshape(-1), cls_b.grad])
    flat = np.concatenate(dp).astype(np.float32)
    assert len(flat) == TOTAL, \
        "gradient is %d long, expected %d" % (len(flat), TOTAL)

    print("logits   absmax %.6f" % logits.abs().max().item())
    print("emb      absmax %.6f" % emb.abs().max().item())
    print("dx       absmax %.6f" % x.grad.abs().max().item())
    print("dparam   absmax %.6f" % float(np.abs(flat).max()))

    src = f'''// GENERATED by verify/gen_model_oracle.py from PyTorch {torch.__version__}.
// Do not hand-edit: the gate that reads these compares against them.
//
// The end-to-end reference for the whole network. Every LAYER already has its
// own oracle -- the block and its CTSE in block_oracle_test.mbt, the ASP head
// and its BatchNorm1d in asp_oracle_test.mbt, the post-MFA SE in
// se_oracle_test.mbt -- but the WIRING between them had never been checked
// against anything.
//
// That is the gap, and it is a gap of a particular kind. If the three TCN stacks
// were concatenated as [long, mid, short] instead of [short, mid, long], the
// forward would be wrong and the backward wrong in exactly the matching way.
// Every finite difference would agree with both, the loss would still fall, and
// the model would be optimising a network that is not the paper''s.
//
// This is not theoretical: the first run of this gate failed on 12 of 12 logits,
// and the cause was the bottleneck''s 1x1 convolution being routed through
// `linear_forward`, which contracts over a run of consecutive flat elements
// rather than over one frame''s channels. Measured against `F.conv1d` on the
// same array: `linear_forward` was off by 1.45e-01 and `tn_conv1x1_forward` by
// 7.45e-09.
//
// Built from the same {TOTAL}-scalar flat vector the MoonBit side uses, so the two
// cannot silently disagree about the parameters.

///|
///|```text
///|MODEL_N / MODEL_T / MODEL_CLASSES / MODEL_NPARAMS
///|```

///|
///|```text
///|MODEL_N
///|"""

pub const MODEL_N : Int = {N}

///|
///|```text
///|MODEL_T
///|"""

pub const MODEL_T : Int = {T}

///|
///|```text
///|MODEL_CLASSES
///|"""

pub const MODEL_CLASSES : Int = {CLASSES}

///|
///|```text
///|MODEL_NPARAMS
///|"""

pub const MODEL_NPARAMS : Int = {TOTAL}

///|
///|```text
///|model_logits_golden
///|"""

pub let model_logits_golden : Array[Float] = {fmt(logits.detach().numpy())}

///|
///|```text
///|model_emb_golden
///|"""

pub let model_emb_golden : Array[Float] = {fmt(emb.detach().numpy())}

///|
///|```text
///|model_dx_golden
///|"""

pub let model_dx_golden : Array[Float] = {fmt(x.grad.numpy())}

///|
///|```text
///|model_dp_golden : all {TOTAL} parameter gradients, in tarnet_flatten's order
///|"""

pub let model_dp_golden : Array[Float] = {fmt(flat)}
'''
    out = os.path.join(ROOT, "mbt", "tests", "wave2", "model_oracle_golden.mbt")
    with io.open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(src)
    print("wrote %s" % out)


if __name__ == "__main__":
    main()
