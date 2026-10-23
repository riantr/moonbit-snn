# gen_block_oracle.py -- a PyTorch autograd oracle for TARNet's Conv1DBlock.
#
# WHY THIS EXISTS. Every gradient in `mbt/tarnet/` is currently checked against
# central differences of its own forward, which is blind to a forward that is
# wrong. That is not hypothetical: `asp_forward` wrote its context buffer at
# the wrong stride and `batch_norm2d_forward` cancelled catastrophically in its
# variance, and the finite-difference suite was green through both. The ASP head
# already has an oracle (`verify/asp_oracle.py`). This one covers the BLOCK,
# which is the most error-prone composition in the model -- two 1x1
# convolutions, two global layer norms, a depthwise dilated convolution, two
# PReLUs and the paper's CTSE gate -- and which is instantiated 5 times in the
# small fixture and 12 at the paper's default width. One block check covers all
# of them.
#
# TRANSCRIBED FROM THE REFERENCE, not from the MoonBit port. The source is
# `_build/tarnet_ref/tarnet/models/tarnet.py`, classes `ChannelTimeSenseSELayer`
# and `Conv1DBlock`.
#
# ONE DOCUMENTED DEVIATION IS CARRIED. The reference declares TWO separate
# `nn.PReLU()` layers in its `shared` stack; this port uses ONE slope at both
# sites, and `tarnet.mbt` says so. This oracle therefore models a single shared
# slope, which is a one-line substitution in an otherwise faithful transcription.
# The alternative -- emitting goldens the port cannot match -- would test the
# deviation rather than the block.
#
# GLOBAL LAYER NORM is not defined in the reference either: `tarnet.py` gets it
# from `asteroid.masknn.norms.get("gLN")`, and asteroid is neither vendored here
# nor installed. It is implemented below from its documented definition --
# statistics over the whole (C, T) plane of each sample, per-channel affine --
# which is what this port does. eps is taken from the exported parameters rather
# than assumed.
#
# Run:  python verify/gen_block_oracle.py <export.log>

import io
import os
import sys

import numpy as np
import torch
import torch.nn.functional as F

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BLOCK_PARAMS = 449
HEADER = 68          # ln_in.gamma(6) + ln_in.beta(6) + bn_w(48) + bn_b(8)
EPS = 1.0e-8         # this port's GlobalLayerNorm floor; see nn/layer_norm.mbt
BLOCKS = [
    # (index, stack, dilation)
    (0, "s", 1),
    (1, "s", 2),
    (2, "m", 4),
    (3, "m", 8),
    (4, "l", 16),
]
N, C, T, HID, KERNEL = 3, 8, 6, 8, 3
CTSE_K = (3, 5, 9)


def parse(path):
    # The export is produced by PowerShell's `*> log`, which on Windows PowerShell
    # writes UTF-16LE. Reading it as UTF-8 leaves NUL between every character and
    # every `find` silently returns -1 -- so the encoding is detected rather than
    # assumed, and a miss reports what was actually read instead of "missing".
    raw = open(path, "rb").read()
    for enc in ("utf-8", "utf-16", "utf-16-le", "latin-1"):
        txt = raw.decode(enc, errors="replace")
        if "TNORACLE" in txt:
            break
    else:
        sys.exit("no TNORACLE marker found in the export under any encoding")

    out = {}
    # ONLY `params` is read out of the export. `bx` and `bd` used to be
    # required here too, and that made this generator impossible to run: no
    # file in this repository has ever emitted a `TNORACLE bx` line -- not
    # the block test, not the export test, not any commit in the history
    # (`git log --all -S 'TNORACLE bx'` returns nothing) -- so `parse` exited
    # with "missing bx in the export" before a single number was computed.
    # The golden this script writes was therefore not produced by the script
    # that ships with it.
    #
    # The fixture does not need exporting. `blk_fixture` below is an
    # integer-hash fixture -- no RNG, no shared state -- and it is the same
    # fixture `block_oracle_test.mbt` builds, down to the last bit, because
    # both sides compute it with integer arithmetic and one division.
    for key in ("params",):
        i = txt.find("TNORACLE " + key + " ")
        if i < 0:
            sys.exit("missing %s in the export" % key)
        line = txt[i:txt.index("\n", i)]
        vals = line.split()[2:]
        try:
            out[key] = [float(v) for v in vals]
        except ValueError as e:
            sys.exit("could not parse %s (%s): %r" % (key, e, vals[:5]))
    return out


def blk_fixture(seed):
    """The block oracle's fixture, transcribed from `blk_fixture` in
    `mbt/tests/wave2/block_oracle_test.mbt`.

    Integer arithmetic plus one division, so the two sides agree exactly:
    the same hash, the same per-channel base and scale, the same rounding.

        base_v = 0.5F * ch - 2.0F
        scale  = 0.3F + 0.2F * ch
        h      = ((b * t + ti) * 37 + ch * 101 + seed) % 71 - 35
        out    = base_v + scale * h / 35.0F

    Seed 0 is the input, seed 500 is the cotangent the backward is taken
    against -- the two the MoonBit test uses.
    """
    x = np.zeros((N, C, T), dtype=np.float32)
    for b in range(N):
        for c in range(C):
            base = 0.5 * c - 2.0
            scale = 0.3 + 0.2 * c
            for ti in range(T):
                h = ((b * T + ti) * 37 + c * 101 + seed) % 71 - 35
                x[b, c, ti] = base + scale * h / 35.0
    return torch.tensor(x)


def prelu(x, slope):
    """PReLU with ONE slope shared by every channel.

    `F.prelu` wants one parameter per channel, which cannot express a shared
    slope -- and sharing is the port's documented deviation from the
    reference's two separate `nn.PReLU()` layers. Written out so the gradient
    with respect to that single scalar is well defined.
    """
    return torch.where(x > 0, x, x * slope)


class CTSE(torch.nn.Module):
    """ChannelTimeSenseSELayer, transcribed from the reference."""

    def __init__(self, c, c_red, ks, params):
        super().__init__()
        self.c, self.c_red = c, c_red
        self.ks = ks
        self.conv = torch.nn.ModuleList([
            torch.nn.Conv1d(c, c, k, padding=k // 2, groups=c, bias=True)
            for k in ks
        ])
        self.feature_fc = torch.nn.Linear(3, 1)
        self.fc1 = torch.nn.Linear(c, c_red)
        self.fc2 = torch.nn.Linear(c_red, c)
        self.load(params)

    def load(self, p):
        c, c_red = self.c, self.c_red
        k = 0
        for m in self.conv:
            w = m.weight.detach().reshape(-1)
            m.weight.data = w.clone().view_as(m.weight)
            k += w.numel()
            m.bias.data = torch.tensor(p[k:k + c], dtype=torch.float32)
            k += c
        self.feature_fc.weight.data = torch.tensor([p[k:k + 3]], dtype=torch.float32)
        k += 3
        self.feature_fc.bias.data = torch.tensor([p[k]], dtype=torch.float32)
        k += 1
        self.fc1.weight.data = torch.tensor(
            p[k:k + c_red * c], dtype=torch.float32).view(c_red, c)
        k += c_red * c
        self.fc1.bias.data = torch.tensor(p[k:k + c_red], dtype=torch.float32)
        k += c_red
        self.fc2.weight.data = torch.tensor(
            p[k:k + c * c_red], dtype=torch.float32).view(c, c_red)
        k += c * c_red
        self.fc2.bias.data = torch.tensor(p[k:k + c], dtype=torch.float32)
        k += c
        assert k == len(p), "CTSE slice length %d != %d" % (k, len(p))

    def forward(self, x):
        feats = []
        for m in self.conv:
            y = F.adaptive_avg_pool1d(m(x), 1)
            feats.append(F.relu(y))
        feat = torch.cat(feats, dim=2)                 # (B, C, 3)
        squeeze = self.feature_fc(feat)[..., 0]         # (B, C)
        h = F.relu(self.fc1(squeeze))                   # (B, C_red)
        w = torch.sigmoid(self.fc2(h))                  # (B, C)
        return x * w.view(x.size(0), x.size(1), 1)


class GLN(torch.nn.Module):
    """Global layer norm over the whole (C, T) plane of each sample."""

    def __init__(self, c, gamma, beta, eps):
        super().__init__()
        self.gamma = torch.nn.Parameter(torch.tensor(gamma, dtype=torch.float32))
        self.beta = torch.nn.Parameter(torch.tensor(beta, dtype=torch.float32))
        self.eps = eps

    def forward(self, x):
        mean = x.mean(dim=(1, 2), keepdim=True)
        var = (x - mean).pow(2).mean(dim=(1, 2), keepdim=True)
        return self.gamma.view(1, -1, 1) * (x - mean) / torch.sqrt(var + self.eps) \
            + self.beta.view(1, -1, 1)


class Block(torch.nn.Module):
    """Conv1DBlock, transcribed from the reference, with ONE shared PReLU."""

    def __init__(self, chan, hid, kernel, dilation, c_red, params):
        super().__init__()
        k = 0
        self.pw1 = torch.nn.Conv1d(chan, hid, 1, bias=True)
        self.pw1.weight.data = torch.tensor(
            params[k:k + hid * chan], dtype=torch.float32).view(hid, chan, 1)
        k += hid * chan
        self.pw1.bias.data = torch.tensor(params[k:k + hid], dtype=torch.float32)
        k += hid
        self.ln1 = GLN(hid, params[k:k + hid], params[k + hid:k + 2 * hid], EPS)
        k += 2 * hid
        pad = (kernel - 1) * dilation // 2
        self.dw = torch.nn.Conv1d(hid, hid, kernel, padding=pad,
                                  dilation=dilation, groups=hid, bias=True)
        self.dw.weight.data = torch.tensor(
            params[k:k + hid * kernel], dtype=torch.float32).view(hid, 1, kernel)
        k += hid * kernel
        self.dw.bias.data = torch.tensor(params[k:k + hid], dtype=torch.float32)
        k += hid
        self.prelu = torch.nn.Parameter(torch.tensor([params[k]], dtype=torch.float32))
        k += 1
        self.ln2 = GLN(hid, params[k:k + hid], params[k + hid:k + 2 * hid], EPS)
        k += 2 * hid
        self.pw2 = torch.nn.Conv1d(hid, chan, 1, bias=True)
        self.pw2.weight.data = torch.tensor(
            params[k:k + chan * hid], dtype=torch.float32).view(chan, hid, 1)
        k += chan * hid
        self.pw2.bias.data = torch.tensor(params[k:k + chan], dtype=torch.float32)
        k += chan
        self.ctse = CTSE(chan, c_red, CTSE_K, params[k:])
        assert k + len(params[k:]) == BLOCK_PARAMS

    def forward(self, x):
        r = prelu(self.pw1(x), self.prelu)
        r = self.ln1(r)
        r = prelu(self.dw(r), self.prelu)
        r = self.ln2(r)
        r = self.pw2(r)
        r = self.ctse(r)
        return x + r


def flat_param_grad(b):
    """Parameter gradients in `tarnet_block_grad_flatten`'s order.

    That order is: pw1 weight then bias, ln1 gamma then beta, depthwise weight
    then bias, the single shared prelu slope, ln2 gamma then beta, pw2 weight
    then bias, then `ctse_grad_flatten` -- which mirrors `ctse_flatten` itself:
    small w/b, mid w/b, large w/b, feature_fc weight then bias, fc1 w/b,
    fc2 w/b.

    Length is 449, matching the block's 449 parameters, which is what makes the
    two flat vectors positionally comparable without an index map.
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
    assert len(out) == BLOCK_PARAMS, "gradient flat is %d, expected %d" % (
        len(out), BLOCK_PARAMS)
    return np.array(out, dtype=np.float32)


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
    if len(sys.argv) < 2:
        sys.exit("usage: gen_block_oracle.py <export.log>")
    data = parse(sys.argv[1])
    params = data["params"]
    bx = blk_fixture(0)
    bd = blk_fixture(500)

    print("nparam %d  (header %d + %d blocks x %d = %d)"
          % (len(params), HEADER, len(BLOCKS), BLOCK_PARAMS,
             HEADER + len(BLOCKS) * BLOCK_PARAMS))
    assert len(params) == 3573, "unexpected parameter count"

    x = bx.clone().requires_grad_(True)
    b = Block(C, HID, KERNEL, 1, 4, params[HEADER:HEADER + BLOCK_PARAMS])
    y = b(x)
    (y * bd).sum().backward()

    # sanity: the oracle's own forward is not constant and not degenerate
    assert y.std().item() > 1e-3, "block output is suspiciously flat"
    print("out absmax %.6f   dx absmax %.6f"
          % (y.abs().max().item(), x.grad.abs().max().item()))

    # goldens for every block, since each is the same code at a new dilation
    chunks = []
    for i, stack, dil in BLOCKS:
        off = HEADER + i * BLOCK_PARAMS
        xi = bx.clone().requires_grad_(True)
        bb = Block(C, HID, KERNEL, dil, 4, params[off:off + BLOCK_PARAMS])
        yi = bb(xi)
        (yi * bd).sum().backward()
        print("block %d stack_%s dilation %2d : out absmax %.6f  dx absmax %.6f"
              % (i, stack, dil, yi.abs().max().item(), xi.grad.abs().max().item()))
        chunks.append((i, stack, dil, yi.detach().numpy(), xi.grad.numpy(),
                       flat_param_grad(bb)))

    src = [f'''// GENERATED by verify/gen_block_oracle.py from PyTorch {torch.__version__}.
// Do not hand-edit: the gate that reads these compares against them.
//
// A PyTorch autograd reference for TARNet's Conv1DBlock, transcribed from
// `_build/tarnet_ref/tarnet/models/tarnet.py` (classes Conv1DBlock and
// ChannelTimeSenseSELayer) rather than from this port, so a transcription
// error in the MoonBit block shows up as a disagreement instead of being
// reproduced on both sides.
//
// The block is the model\'s most error-prone composition and is instantiated 5
// times in the small fixture and 12 at the paper\'s default width, so one
// implementation of the check covers every block in the network.
//
// ONE DEVIATION IS CARRIED, and it is the port\'s: the reference declares two
// separate `nn.PReLU()` layers in its `shared` stack, this port uses one
// slope at both sites, and so does this oracle. `tarnet.mbt` documents it.
//
// The global layer norm is implemented from asteroid\'s documented definition,
// because `tarnet.py` obtains it from `asteroid.masknn.norms.get("gLN")` and
// asteroid is neither vendored here nor installed. See nn/layer_norm.mbt.

///|
///|```text
///|BLOCK_N / BLOCK_C / BLOCK_T : the block oracle fixture shape
///|```

///|
///|```text
///|BLOCK_N
///|"""

pub const BLOCK_N : Int = {N}

///|
///|```text
///|BLOCK_C
///|"""

pub const BLOCK_C : Int = {C}

///|
///|```text
///|BLOCK_T
///|"""

pub const BLOCK_T : Int = {T}

///|
///|```text
///|block_dilations : one per block, in tarnet_flatten\'s order
///|"""

///|
///|```text
///|block_dilations
///|"""

pub let block_dilations : Array[Int] = [''' + ", ".join(str(d) for _, _, d in BLOCKS) + ''']
''']

    for i, stack, dil, yo, xg, dp in chunks:
        src.append(f'''
///|
///|```text
///|blk{i}_out_golden : block {i} (stack_{stack}, dilation {dil}) forward
///|"""

///|
///|```text
///|blk{i}_out_golden
///|"""

pub let blk{i}_out_golden : Array[Float] = {fmt(yo)}

///|
///|```text
///|blk{i}_dx_golden : d(sum(out*d_out))/d x for block {i}
///|"""

///|
///|```text
///|blk{i}_dx_golden
///|"""

pub let blk{i}_dx_golden : Array[Float] = {fmt(xg)}

///|
///|```text
///|blk{i}_dp_golden : the 449 parameter gradients, in
///|```text
///|tarnet_block_grad_flatten's order
///|"""

///|
///|```text
///|blk{i}_dp_golden
///|"""

pub let blk{i}_dp_golden : Array[Float] = {fmt(dp)}''')

    out_path = os.path.join(ROOT, "mbt", "tests", "wave2", "block_oracle_golden.mbt")
    with io.open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(src) + "\n")
    print("wrote %s" % out_path)


if __name__ == "__main__":
    main()
