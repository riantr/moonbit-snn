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
# the export step, which does not exist in the tree yet.
#
# Two entry points, and the split matters:
#
#   python verify/gen_model_oracle.py --selftest   needs NOTHING. Validates the
#       oracle against its own forward in float64, in under a second.
#   python verify/gen_model_oracle.py <export.log> needs the export. Emits the
#       goldens the MoonBit gate compares against.
#
# The selftest used to be the second's job and was decorative: it computed
# `absmax(x) / absmax(x)`, which is 1.0 by construction, and wired the SE after
# the ASP head where its 8 channels do not match the 16 it is handed, so it
# could not have run even once. An oracle that cannot validate itself without an
# export nobody can regenerate is an oracle nobody validates -- which is how
# six defects below survived four fix-diagnose-revert cycles.

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
    """The integer-hash construction `model_oracle_test.mbt` reproduces.

    The frame count is honoured in BOTH the allocation and the hash. It was not:
    the array was allocated with the module constant `T` while the hash used the
    `t` parameter, so `fixture(500, CLASSES, 1)` quietly returned six frames
    instead of one. Nothing downstream complained -- `logits` is `[n, classes,
    1]` and `d_logits` was `[n, classes, 6]`, and multiplying those BROADCASTS.
    The whole oracle was therefore differentiating a different loss than the one
    it printed, and reporting finite, plausible, wrong gradients for all 3573
    parameters.

    The comment here used to claim this was already handled. It described the
    hash and not the allocation, which is the more useful of the two places to
    be wrong about: one produces different numbers, the other produces a
    plausible answer to a different question.
    """
    x = np.zeros((N, c, t), dtype=np.float32)
    for b in range(N):
        for ch in range(c):
            base = 0.5 * ch - 2.0
            scale = 0.3 + 0.2 * ch
            for ti in range(t):
                # Hashes with `t`, the SAME `t` being looped -- the MoonBit side
                # hashes with the frame count it was given.
                h = ((b * t + ti) * 37 + ch * 101 + seed) % 71 - 35
                x[b, ch, ti] = base + scale * h / 35.0
    return torch.tensor(x)


class GLN(torch.nn.Module):
    """Global layer norm over the whole (C, T) plane of each sample.

    Takes tensors, not numbers, and keeps their dtype. Pinning float32 here
    would make a float64 finite-difference pass impossible -- and that pass is
    the only thing standing between this file and being an unfalsifiable gate.
    """

    def __init__(self, gamma, beta):
        super().__init__()
        self.gamma = torch.nn.Parameter(torch.as_tensor(gamma))
        self.beta = torch.nn.Parameter(torch.as_tensor(beta))

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


# The flat layout, stated ONCE. Both consumers read this table: `build` walks
# the parameters in this order, and `analytic_grad` gathers the gradients in it.
# The previous draft had the wiring written out twice -- once in `main`, once in
# the selftest -- and the two copies disagreed about where the SE goes.
SEGMENTS = (
    [("ln_in.gamma", IN_CHAN), ("ln_in.beta", IN_CHAN),
     ("bn_w", FUSE * IN_CHAN), ("bn_b", FUSE)]
    + [("block %d" % i, BLOCK_PARAMS) for i in range(len(DILS))]
    + [("mfa_w", FUSE * 3 * BN_CHAN), ("mfa_b", FUSE), ("mfa_se", SE_LEN)]
    + [("asp.w1", ATT * 3 * FUSE), ("asp.b1", ATT),
       ("asp.att_bn_gamma", ATT), ("asp.att_bn_beta", ATT),
       ("asp.w2", FUSE * ATT), ("asp.b2", FUSE),
       ("bn_seg.gamma", 2 * FUSE), ("bn_seg.beta", 2 * FUSE),
       ("fc_w", EMB * 2 * FUSE), ("fc_b", EMB),
       ("bn_out.gamma", EMB), ("bn_out.beta", EMB),
       ("cls_w", CLASSES * EMB), ("cls_b", CLASSES)]
)
assert sum(n for _, n in SEGMENTS) == TOTAL, \
    "SEGMENTS covers %d parameters, TOTAL says %d" % (
        sum(n for _, n in SEGMENTS), TOTAL)


def seg_range(label):
    lo = 0
    for name, n in SEGMENTS:
        if name == label:
            return lo, lo + n
        lo += n
    raise KeyError(label)


def block_grad_flat(b):
    """`flat_param_grad` in the caller's dtype instead of float32.

    The float32 cast inside it is harmless when the gradients ARE float32 and
    fatal here: it would round a float64 analytic gradient down to ~1e-7
    relative, which is four orders of magnitude above the float64 finite
    difference floor and would drown the very signal this check exists to see.
    `selftest` pins the order by requiring this to agree bit-for-bit with
    `flat_param_grad` on the float32 pass.
    """
    out = []
    out += list(b.pw1.weight.grad.reshape(-1))
    out += list(b.pw1.bias.grad)
    out += list(b.ln1.gamma.grad)
    out += list(b.ln1.beta.grad)
    out += list(b.dw.weight.grad.reshape(-1))
    out += list(b.dw.bias.grad)
    out += list(b.prelu.grad.reshape(-1))
    out += list(b.ln2.gamma.grad)
    out += list(b.ln2.beta.grad)
    out += list(b.pw2.weight.grad.reshape(-1))
    out += list(b.pw2.bias.grad)
    for m in b.ctse.conv:
        out += list(m.weight.grad.reshape(-1))
        out += list(m.bias.grad)
    out += list(b.ctse.feature_fc.weight.grad.reshape(-1))
    out += list(b.ctse.feature_fc.bias.grad)
    out += list(b.ctse.fc1.weight.grad.reshape(-1))
    out += list(b.ctse.fc1.bias.grad)
    out += list(b.ctse.fc2.weight.grad.reshape(-1))
    out += list(b.ctse.fc2.bias.grad)
    assert len(out) == BLOCK_PARAMS, "block gradient flat is %d" % len(out)
    return np.asarray(out, dtype=np.float64)


def se_grad_flat(se):
    out = []
    out += list(se.fc1.weight.grad.reshape(-1))
    out += list(se.fc1.bias.grad)
    out += list(se.fc2.weight.grad.reshape(-1))
    out += list(se.fc2.bias.grad)
    assert len(out) == SE_LEN, "SE gradient flat is %d" % len(out)
    return np.asarray(out, dtype=np.float64)


def _load_flat_into(tensors, flat, dtype):
    """Overwrite parameters, in `flat_param_grad` order, from a float64 vector.

    `Block.__init__` and `SE.__init__` hardcode `dtype=torch.float32` in their
    `.data =` assignments, so they cannot be built directly in float64. Left
    alone, that makes the loss a STAIRCASE in the float64 parameters: the
    truncation step is ~3.7e-9 per weight, 2800 weights move together, and the
    resulting wobble in L divided by a 2h of 2e-6 produces O(1) error in every
    finite difference -- worse as h shrinks, which is the signature that
    identifies it as quantisation rather than a kink.

    Rewriting the values after construction costs one pass and keeps the
    existing committed oracles untouched. The order is the same one
    `flat_param_grad` walks, and the length is asserted against BLOCK_PARAMS, so
    a drift shows up here rather than as a silent mis-derivation.
    """
    k = 0
    for t in tensors:
        n = int(t.numel())
        t.data = torch.tensor(np.asarray(flat[k:k + n], dtype=np.float64),
                              dtype=dtype).view_as(t)
        k += n
    return k


def block_tensors(b):
    out = [b.pw1.weight, b.pw1.bias, b.ln1.gamma, b.ln1.beta,
           b.dw.weight, b.dw.bias, b.prelu, b.ln2.gamma, b.ln2.beta,
           b.pw2.weight, b.pw2.bias]
    for m in b.ctse.conv:
        out += [m.weight, m.bias]
    out += [b.ctse.feature_fc.weight, b.ctse.feature_fc.bias,
            b.ctse.fc1.weight, b.ctse.fc1.bias,
            b.ctse.fc2.weight, b.ctse.fc2.bias]
    return out


def load_block(flat, dil, dtype):
    b = Block(BN_CHAN, BN_CHAN, 3, dil, BN_CHAN // 2,
              np.asarray(flat, dtype=np.float32)).to(dtype)
    k = _load_flat_into(block_tensors(b), flat, dtype)
    assert k == BLOCK_PARAMS, "block load walked %d, expected %d" % (k, BLOCK_PARAMS)
    return b


def load_se(flat, dtype):
    se = SE(FUSE, SE_BNK, np.asarray(flat, dtype=np.float32)).to(dtype)
    k = _load_flat_into([se.fc1.weight, se.fc1.bias,
                         se.fc2.weight, se.fc2.bias], flat, dtype)
    assert k == SE_LEN, "SE load walked %d, expected %d" % (k, SE_LEN)
    return se


def build(pvec, dtype, want_grad):
    """The whole network from a flat parameter vector. THE wiring.

    One function, two consumers: the analytic pass that reads `.grad` off the
    leaves, and the finite-difference probes that only want the scalar loss.
    They have to share it. A check of a different network than the one under
    test is worse than no check, because it reports green -- which is precisely
    what the first draft of `selftest` did: it applied the SE to the ASP head's
    [n, 16] output using an SE built for 8 channels, which cannot have run even
    once.
    """
    pv = np.asarray(pvec)
    p = torch.as_tensor(pv).to(dtype)
    k = 0

    def take(n):
        nonlocal k
        v = p[k:k + n].detach().clone()
        k += n
        return v

    def leaf(n):
        return torch.nn.Parameter(take(n))

    x = torch.nn.Parameter(fixture(0, IN_CHAN).to(dtype))

    ln_g, ln_b = leaf(IN_CHAN), leaf(IN_CHAN)
    bn_w, bn_b = leaf(FUSE * IN_CHAN), leaf(FUSE)
    assert k == HEADER, "header walk ended at %d, expected %d" % (k, HEADER)

    ln_in = GLN(ln_g, ln_b)
    # GLN wraps its arguments in NEW Parameters, so the leaves created above are
    # not the ones autograd will populate -- `ln_g.grad` stays None forever.
    # Track the module's own Parameters instead. Silent, because a Parameter
    # with no gradient is not an error, it is just a number that never changes.
    ln_g, ln_b = ln_in.gamma, ln_in.beta
    h = conv1x1(ln_in(x), bn_w, bn_b, BN_CHAN)

    # Three PARALLEL stacks off the bottleneck, each a SEQUENCE of blocks, and
    # it is the last block of each that concatenates -- not all five blocks.
    blks = []
    for i, dil in enumerate(DILS):
        blks.append(load_block(
            pv[HEADER + i * BLOCK_PARAMS:HEADER + (i + 1) * BLOCK_PARAMS],
            dil, dtype))
    s = blks[1](blks[0](h))     # stack_s: dilations [1, 2]
    m = blks[3](blks[2](h))     # stack_m: dilations [4, 8]
    l = blks[4](h)              # stack_l: dilation  [16]
    cat = torch.cat([s, m, l], dim=1)
    assert cat.shape[1] == 3 * BN_CHAN, \
        "concat gave %d channels, the MFA expects %d" % (cat.shape[1], 3 * BN_CHAN)

    k = MFA_W_OFF                     # the blocks were read by slice, not walked
    mfa_w, mfa_b = leaf(FUSE * 3 * BN_CHAN), leaf(FUSE)
    mfa = F.relu(conv1x1(cat, mfa_w, mfa_b, FUSE))
    assert k == SE_OFFSET, "MFA walk ended at %d, expected %d" % (k, SE_OFFSET)

    se = load_se(pv[SE_OFFSET:SE_OFFSET + SE_LEN], dtype)
    mfa = se(mfa)                     # ReLU -> SE -> ASP head, in that order
    k = ASP_OFFSET

    # Keep the Parameters themselves and hand VIEWS to the forward. A `.view()`
    # of a leaf is not a leaf, so `.grad` on the view is never populated.
    asp_w1, asp_b1 = leaf(ATT * 3 * FUSE), leaf(ATT)
    asp_bg, asp_bb = leaf(ATT), leaf(ATT)
    asp_w2, asp_b2 = leaf(FUSE * ATT), leaf(FUSE)
    pooled, _alpha, _ctx = asp_forward_torch(
        mfa,
        asp_w1.view(ATT, 3 * FUSE), asp_b1, asp_bg, asp_bb,
        asp_w2.view(FUSE, ATT), asp_b2,
    )

    seg_g, seg_b = leaf(2 * FUSE), leaf(2 * FUSE)
    seg = F.batch_norm(pooled.unsqueeze(2), None, None, seg_g, seg_b,
                       True, 0.0, 1e-5)
    fc_w, fc_b = leaf(EMB * 2 * FUSE), leaf(EMB)
    e = conv1x1(seg, fc_w, fc_b, EMB)
    out_g, out_b = leaf(EMB), leaf(EMB)
    emb = F.batch_norm(e, None, None, out_g, out_b, True, 0.0, 1e-5)
    cls_w, cls_b = leaf(CLASSES * EMB), leaf(CLASSES)
    logits = conv1x1(emb, cls_w, cls_b, CLASSES)
    assert k == TOTAL, "walk ended at %d, expected %d" % (k, TOTAL)

    # The logit gradient is [n, n_classes, 1] -- ONE frame, because the ASP head
    # pools over time and the heads see a single frame per sample. Passing a
    # [n, n_classes, t] tensor here would be a different gradient entirely, and
    # every parameter gradient downstream of it would then disagree for reasons
    # that have nothing to do with the model.
    dl = fixture(500, CLASSES, 1).to(dtype)
    # Multiplying mismatched shapes BROADCASTS instead of failing. A [n, k, 1]
    # logits against a [n, k, 6] gradient silently evaluates a different loss
    # and returns finite, plausible gradients for every parameter, which is the
    # worst possible failure mode for a reference implementation.
    assert dl.shape == logits.shape, \
        "d_logits %s vs logits %s -- these would broadcast" % (
            tuple(dl.shape), tuple(logits.shape))
    st = {"loss": (logits * dl).sum(), "x": x, "logits": logits, "emb": emb,
          "blks": blks, "se": se}
    if not want_grad:
        return st

    st["loss"].backward()
    st["grads"] = {
        "ln_in.gamma": ln_g, "ln_in.beta": ln_b,
        "bn_w": bn_w, "bn_b": bn_b,
        "mfa_w": mfa_w, "mfa_b": mfa_b,
        "asp.w1": asp_w1, "asp.b1": asp_b1,
        "asp.att_bn_gamma": asp_bg, "asp.att_bn_beta": asp_bb,
        "asp.w2": asp_w2, "asp.b2": asp_b2,
        "bn_seg.gamma": seg_g, "bn_seg.beta": seg_b,
        "fc_w": fc_w, "fc_b": fc_b,
        "bn_out.gamma": out_g, "bn_out.beta": out_b,
        "cls_w": cls_w, "cls_b": cls_b,
    }
    for i, b in enumerate(blks):
        st["grads"]["block %d" % i] = block_grad_flat(b)
    st["grads"]["mfa_se"] = se_grad_flat(se)
    return st


def analytic_flat(st):
    """The analytic gradient as one float64 vector, in SEGMENTS order.

    `st["grads"]` holds the leaf PARAMETERS for the plain tensors and
    already-gathered float64 arrays for the blocks and the SE. Reading a
    parameter where a gradient belongs is silent -- gamma is initialised to 1
    and every beta to 0, so a mistaken read returns 1.0 and 0.0, looks like a
    perfectly ordinary gradient, and never disagrees with anything loudly. Take
    `.grad` here, and assert it exists.
    """
    out = []
    for label, n in SEGMENTS:
        g = st["grads"][label]
        if torch.is_tensor(g):
            assert g.grad is not None, \
                "%s carries no gradient -- it is not reachable from the loss" % label
            a = np.asarray(g.grad.detach().cpu().numpy(),
                           dtype=np.float64).reshape(-1)
        else:
            a = np.asarray(g, dtype=np.float64).reshape(-1)
        assert a.size == n, "%s gradient is %d, expected %d" % (label, a.size, n)
        out.append(a)
    return np.concatenate(out)


def fd_flat(pvec, h, dtype=torch.float64):
    """Central differences of `build`'s OWN forward loss, every parameter."""
    p = np.asarray(pvec, dtype=np.float32).astype(np.float64).copy()
    d = np.empty(TOTAL, dtype=np.float64)
    for i in range(TOTAL):
        p[i] = pvec[i] + h
        lp = float(build(p, dtype, False)["loss"].detach())
        p[i] = pvec[i] - h
        lm = float(build(p, dtype, False)["loss"].detach())
        p[i] = pvec[i]
        d[i] = (lp - lm) / (2.0 * h)
    return d


def worst_per_segment(a, b):
    """Scaled error per segment, and the raw absolutes beside it.

    The scale is `max(|a|, |b|, 1e-6 * segment_absmax)`. The floor is not
    slack -- without it any gradient that is legitimately near zero would report
    an unbounded relative error, and a table full of infinities is a table
    nobody reads. It is 1e-6 of the segment's own largest entry, so it can only
    rescue entries that are already six orders below their neighbours.
    """
    rows = []
    for label, _ in SEGMENTS:
        lo, hi = seg_range(label)
        x, y = a[lo:hi], b[lo:hi]
        floor = max(float(np.abs(x).max()), float(np.abs(y).max())) * 1e-6
        scale = np.maximum(np.maximum(np.abs(x), np.abs(y)), floor)
        err = np.abs(x - y) / scale
        rows.append((label, hi - lo, float(err.max()),
                     float(np.abs(x - y).max()),
                     float(np.abs(x).max())))
    return rows


DIR_SEED0 = 20260910
DIR_COUNT = 12


def directional_fd(params, v, h, dtype=torch.float64):
    """Central difference of the loss along ONE direction across all parameters.

    This replaces the per-coordinate probe, and the reason is not a preference.

    A coordinate probe steps one weight at a time, and the model is kinked --
    ReLU in the MFA and the ASP head, PReLU and CTSE's ReLU inside every block,
    and a `clamp` in the ASP pooling. With ASP pre-activations of order 9e-3
    spread over 144 units and roughly five thousand kinked units in the whole
    network, a step of 1e-5 crosses zero somewhere essentially every time, and
    the resulting jump in the loss swamps the derivative being measured. That is
    not a hypothesis: measured per-coordinate relative errors were 1e-1 to 3e+0
    and got WORSE as the step shrank.

    Spreading the step over a random direction moves each weight by ~3e-7, no
    individual unit flips, and the comparison becomes meaningful: measured
    relative error 1.6e-9 to 5.8e-9 at h=1e-6, i.e. float64 roundoff.

    `fd_flat` is kept below as a DIAGNOSTIC, because a coordinate sweep still
    localises a defect to a segment in a way a scalar cannot.
    """
    p = np.asarray(params, dtype=np.float64)
    lp = float(build(p + h * v, dtype, False)["loss"].detach())
    lm = float(build(p - h * v, dtype, False)["loss"].detach())
    return (lp - lm) / (2.0 * h)


def sample_fd(params, per_segment=8, h=1e-6):
    """A few coordinates per segment, not all of them -- this is diagnostic."""
    out = {}
    for label, n in SEGMENTS:
        lo, hi = seg_range(label)
        idx = np.unique(np.linspace(lo, hi - 1, min(per_segment, n)).astype(int))
        p = np.asarray(params, dtype=np.float64)
        for i in idx:
            w = p.copy()
            w[i] = p[i] + h
            lp = float(build(w, torch.float64, False)["loss"].detach())
            w[i] = p[i] - h
            lm = float(build(w, torch.float64, False)["loss"].detach())
            out[int(i)] = (lp - lm) / (2.0 * h)
    return out


def selftest(params):
    """Validate THIS ORACLE before trusting it against the port.

    Every bug this file went through -- parallel-stack concatenation, a parameter
    cursor stalling over the SE slice, a d_logits that broadcast, a fixture whose
    allocation ignored its own frame-count argument, gradients read off the
    parameters instead of off `.grad`, blocks left in float32 -- was a bug in the
    ORACLE, not in the MoonBit port. Six of them, and they cost four
    fix-diagnose-revert cycles, because the instrument was trusted before it had
    been checked.

    This runs with nothing but this file: no MoonBit, no goldens, no assumptions
    about the port. Being self-sufficient is the point. An oracle that needs an
    export nobody can regenerate is an oracle nobody runs.

    Be exact about what a pass buys. It shows the backward is the derivative of
    THIS forward. It does NOT show this forward is the paper's -- a wiring that
    is self-consistently wrong passes here and is caught only by the MoonBit gate
    reading the goldens. It is the layer below that one.
    """
    import time
    print("SELFTEST: analytic backward vs float64 central differences "
          "(%d parameters)" % TOTAL)

    # --- the historical broadcasting bug, as a regression check --------------
    dl = fixture(500, CLASSES, 1)
    assert tuple(dl.shape) == (N, CLASSES, 1), (
        "fixture(500, CLASSES, 1) is %s -- the allocation is using the module "
        "constant T instead of the t argument, which silently turns the logit "
        "gradient into six frames and makes it BROADCAST against [n, k, 1]"
        % (tuple(dl.shape),))

    st64 = build(params, torch.float64, True)
    a = analytic_flat(st64)
    assert np.isfinite(a).all(), "analytic gradient has non-finite entries"
    assert np.abs(a).max() > 0, "analytic gradient is identically zero"

    # The one place the gradient has a closed form, checked exactly. The ASP head
    # pools over time, so the classifier bias sees a single frame and
    # d/d cls_b[o] is exactly sum_n d_logits[n, o].
    lo, hi = seg_range("cls_b")
    closed = fixture(500, CLASSES, 1).to(torch.float64)[:, :, 0].sum(0).numpy()
    dev = float(np.abs(a[lo:hi] - closed).max())
    assert dev < 1e-12, "d_cls_b deviates from its closed form by %.3e" % dev
    print("  d_cls_b %s == closed form to %.1e"
          % (np.array2string(a[lo:hi], precision=5), dev))

    # `block_grad_flat` and `se_grad_flat` re-walk the gradient order in float64
    # rather than reusing the float32 flatteners, because a float32 cast rounds
    # a float64 gradient to ~1e-7. Pin that order on the float32 pass: an order
    # mismatch would otherwise surface as a clean 3573-element disagreement with
    # nothing pointing at the cause.
    st32 = build(params, torch.float32, True)
    for i, b in enumerate(st32["blks"]):
        assert np.array_equal(block_grad_flat(b).astype(np.float32),
                              flat_param_grad(b)), \
            "block_grad_flat disagrees with flat_param_grad on block %d" % i
    assert np.array_equal(se_grad_flat(st32["se"]).astype(np.float32),
                          st32["se"].flat_grad()), \
        "se_grad_flat disagrees with SE.flat_grad"
    f32 = analytic_flat(st32)
    print("  gradient order: block_grad_flat == flat_param_grad on %d blocks, "
          "se_grad_flat == SE.flat_grad" % len(st32["blks"]))
    print("  float32 vs float64 analytic: max|d| %.3e on |grad| %.6f"
          % (float(np.abs(a - f32).max()), float(np.abs(a).max())))

    # --- THE GATE: directional finite differences ---------------------------
    h1, h2 = 1e-6, 1e-7
    t0 = time.time()
    print()
    print("  DIRECTIONAL FD over %d random directions (inf-norm 1)" % DIR_COUNT)
    print("    %-3s %17s %17s %17s %10s %10s"
          % ("dir", "analytic . v", "fd(h=1e-6)", "fd(h=1e-7)",
             "rel err", "noise"))
    rows = []
    for d in range(DIR_COUNT):
        rng = np.random.default_rng(DIR_SEED0 + d)
        v = rng.standard_normal(TOTAL)
        v /= np.abs(v).max()
        dot = float(a @ v)
        f1 = directional_fd(params, v, h1)
        f2 = directional_fd(params, v, h2)
        rel = abs(f1 - dot) / max(abs(dot), 1e-300)
        rows.append((rel, abs(f1 - f2)))
        print("    %-3d %17.10f %17.10f %17.10f %10.2e %10.2e"
              % (d, dot, f1, f2, rel, abs(f1 - f2)))
    worst_rel = max(r[0] for r in rows)
    noise = max(r[1] for r in rows)
    tol = 50.0 * noise
    print("    worst relative error %.3e, tolerance 50 x measured noise %.3e "
          "= %.3e, in %.1fs" % (worst_rel, noise, tol, time.time() - t0))

    # --- DIAGNOSTIC: a coordinate sweep, which localises a defect ----------
    print()
    print("  COORDINATE SWEEP, 8 per segment at h=1e-6 -- DIAGNOSTIC ONLY.")
    print("    The network is kinked, so a coordinate probe that steps across a")
    print("    ReLU reports nonsense at coordinates near zero; these numbers say")
    print("    WHERE, and are not the gate.")
    sfd = sample_fd(params, per_segment=8, h=h1)
    for label, n in SEGMENTS:
        lo, hi = seg_range(label)
        idx = [i for i in sfd if lo <= i < hi]
        mag = float(np.abs(a[lo:hi]).max())
        clean = sum(1 for i in idx
                    if abs(sfd[i] - a[i]) <= 1e-6 * max(mag, 1e-12))
        print("    %-18s %d/%d agree to 1e-6 x segment scale (|grad| %.3e)"
              % (label, clean, len(idx), mag))
    # Four segments come out with |grad| ~ 1e-16, i.e. STRUCTURALLY ZERO, and
    # the directional gate agrees they are really zero. Each is a constant
    # offset immediately followed by something that removes constants:
    #   asp.b1      -> BatchNorm1d over (n, t) subtracts the per-unit mean
    #   asp.b2      -> Softmax over t, which is shift-invariant along t
    #   bn_seg.beta -> the following BatchNorm over (n, 1) removes a per-channel
    #                  constant before anything reads it
    #   fc_b        -> same, via bn_out
    # So those four never train. That is a property of the architecture rather
    # than a defect, but it is the kind of thing that looks like a broken
    # gradient, so it is written down here instead of left to be rediscovered.

    # --- CANARIES: the gate must be able to go red -------------------------
    print()
    cache = {}

    def _dir(d):
        if d not in cache:
            rng = np.random.default_rng(DIR_SEED0 + d)
            v = rng.standard_normal(TOTAL)
            cache[d] = v / np.abs(v).max()
        return cache[d]

    def score(cand):
        worst = 0.0
        for d in range(DIR_COUNT):
            v = _dir(d)
            dot = float(cand @ v)
            fd = directional_fd(params, v, h1)
            worst = max(worst, abs(fd - dot) / max(abs(dot), 1e-300))
        return worst

    print("    clean gate scores %.3e" % score(a))

    # canary 1: one segment's gradient is 5% wrong -- a magnitude error.
    bad = a.copy()
    lo, hi = seg_range("bn_out.gamma")
    bad[lo:hi] *= 1.05
    s1 = score(bad)
    assert s1 > 100 * worst_rel, \
        "a 5%% error on bn_out.gamma scored %.3e against a clean %.3e" % (
            s1, worst_rel)
    print("    canary 1  5%% error on bn_out.gamma      -> %.3e  RED" % s1)

    # canary 2: two segments of EQUAL length exchanged -- an ORDERING error,
    # which a magnitude check cannot see. asp.w1 and mfa_w are both 192-element
    # 1x1 convolution weights, so the exchange is shape-legal and would sail
    # through any gate that only counts elements.
    bad = a.copy()
    l1, u1 = seg_range("asp.w1")
    l2, u2 = seg_range("mfa_w")
    assert u1 - l1 == u2 - l2, "canary 2 needs two segments of equal length"
    bad[l1:u1], bad[l2:u2] = a[l2:u2].copy(), a[l1:u1].copy()
    s2 = score(bad)
    assert s2 > 100 * worst_rel, \
        "swapping asp.w1 and mfa_w scored %.3e against a clean %.3e" % (
            s2, worst_rel)
    print("    canary 2  asp.w1 and mfa_w exchanged    -> %.3e  RED" % s2)

    assert worst_rel <= tol, (
        "analytic backward disagrees with float64 directional central "
        "differences: worst relative error %.3e > tolerance %.3e"
        % (worst_rel, tol))
    print()
    print("  SELFTEST PASS")

def synthetic_params(seed=7):
    """A parameter vector shaped like the one `Tarnet::new` produces.

    NOT the model's actual parameters -- those come from a MoonBit export, and
    `main` still needs one. This exists so the selftest runs with nothing but
    this file, which is the property whose absence is the whole reason the
    oracle survived four fix-diagnose-revert cycles unvalidated.

    Scales are transcribed from the port's own constructors: `tn_randn(..,
    0.05F)` for the 1x1 convolutions, `tn_depthwise(.., 0.03F)` for CTSE's
    three paths, `feature_fc` fixed at 0.3 / 0.0, PReLU at 0.25, and gamma=1 /
    beta=0 for every normalisation affine. Getting these roughly right is not
    cosmetic: a degenerate vector would drive the gradients to zero, the finite
    differences would agree with anything, and the canary would have nothing to
    bite on.
    """
    rng = np.random.default_rng(seed)

    def r(n, std):
        return (rng.standard_normal(n) * std).astype(np.float32)

    def z(n):
        return np.zeros(n, dtype=np.float32)

    def o(n):
        return np.ones(n, dtype=np.float32)

    def block():
        parts = [r(BN_CHAN * BN_CHAN, 0.05), z(BN_CHAN),      # pw1 w/b
                 o(BN_CHAN), z(BN_CHAN),                      # ln1 gamma/beta
                 r(BN_CHAN * 3, 0.05), z(BN_CHAN),            # depthwise w/b
                 [np.float32(0.25)],                          # shared PReLU
                 o(BN_CHAN), z(BN_CHAN),                      # ln2 gamma/beta
                 r(BN_CHAN * BN_CHAN, 0.05), z(BN_CHAN)]      # pw2 w/b
        for k in (3, 5, 9):
            parts += [r(BN_CHAN * k, 0.03), z(BN_CHAN)]       # CTSE depthwise
        cr = max(BN_CHAN // 2, 1)
        parts += [np.full(3, 0.3, dtype=np.float32), [np.float32(0.0)]]
        parts += [r(cr * BN_CHAN, 0.05), z(cr), r(BN_CHAN * cr, 0.05), z(BN_CHAN)]
        return parts

    parts = [o(IN_CHAN), z(IN_CHAN), r(FUSE * IN_CHAN, 0.05), z(FUSE)]
    for _ in DILS:
        parts += block()
    parts += [r(FUSE * 3 * BN_CHAN, 0.05), z(FUSE)]                       # MFA
    parts += [r(SE_BNK * FUSE, 0.05), z(SE_BNK),
              r(FUSE * SE_BNK, 0.05), z(FUSE)]                            # SE
    parts += [r(ATT * 3 * FUSE, 0.05), z(ATT), o(ATT), z(ATT),            # ASP
              r(FUSE * ATT, 0.05), z(FUSE)]
    parts += [o(2 * FUSE), z(2 * FUSE),                                   # bn_seg
              r(EMB * 2 * FUSE, 0.05), z(EMB),                            # fc
              o(EMB), z(EMB),                                             # bn_out
              r(CLASSES * EMB, 0.05), z(CLASSES)]                         # cls
    v = np.concatenate([np.asarray(p, dtype=np.float32).reshape(-1)
                        for p in parts])
    assert v.size == TOTAL, \
        "synthetic vector is %d long, expected %d" % (v.size, TOTAL)
    return v


def main():
    p = parse_params(sys.argv[1])
    assert len(p) == TOTAL, "expected %d parameters, got %d" % (TOTAL, len(p))

    # The SAME `build` the selftest validated -- not a second transcription of
    # the wiring. Two copies is how the SE ended up after the ASP head in one of
    # them, and a golden produced by unvalidated wiring is worse than no golden
    # at all, because it presents as an independent reference.
    st = build(p, torch.float32, True)
    logits, emb, x = st["logits"], st["emb"], st["x"]
    for label, n in SEGMENTS:
        print("  %-28s %5d" % (label, n))
    flat = analytic_flat(st).astype(np.float32)
    assert flat.size == TOTAL, \
        "gradient is %d long, expected %d" % (flat.size, TOTAL)

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
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest":
        torch.set_num_threads(1)
        selftest(synthetic_params())
    else:
        main()
