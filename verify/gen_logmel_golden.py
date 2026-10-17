# gen_logmel_golden.py -- an independent reference for TARNet's front-end.
#
# WHY THIS EXISTS. The whole-model autograd gate starts at a log-Mel
# spectrogram, so everything from the bottleneck onwards is checked against
# PyTorch and the front-end is not checked against anything. The existing
# log-Mel gate is not a substitute: its checks are invariants and physics probes
# (FFT vs direct DFT, a pure tone against the band that contains it, symmetric
# vs periodic window, filterbank non-negativity and contiguity). Each is worth
# having, and a uniformly wrong COMPOSITION satisfies every one of them -- wrong
# `center` padding, a wrong hop, or a one-frame offset all pass.
#
# INDEPENDENCE, which is the only property that makes this worth doing. Nothing
# here is ported from `tarnet_logmel.mbt`:
#
#   * the STFT is `torch.stft`, a library call, against the port's hand-rolled
#     radix-2 FFT and bit-reversal;
#   * the window is `torch.hamming_window(periodic=False)`, not the port's
#     `tn_hamming`;
#   * the filterbank is written from the HTK definition here, not copied;
#   * pre-emphasis is `torch.nn.functional.pad(reflect) + conv1d`, matching the
#     reference's `PreEmphasis`, not the port's loop.
#
# Two implementations agreeing is evidence. One implementation agreeing with
# itself is not, which is why this exists next to the invariant suite rather
# than instead of it.
#
# The four stages are exported and compared SEPARATELY, so a disagreement names
# the stage that moved instead of arriving as one number at the end.
#
#   Run:  moon test -p moonbit-snn/mbt/tests/wave2 *> <export.log>
#         python verify/gen_logmel_golden.py <export.log>

import io
import math
import os
import sys

import numpy as np
import torch
import torch.nn.functional as F

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

N_FFT, WIN, HOP, N_MELS = 512, 400, 160, 80
F_MIN, F_MAX, SR = 20.0, 7600.0, 16000
PRE_EMPH = 0.97
CLAMP = 1e-6


# ---- the reference, written from the definitions -----------------------------

def ref_pre_emphasis(wav):
    """`PreEmphasis` as the reference implements it.

    `F.pad(x.unsqueeze(1), (1, 0), mode='reflect')` then `F.conv1d` with the
    kernel `[-coef, 1.0]`. Reflect padding on the LEFT means the first output
    borrows x[1], NOT x[0]: y[0] = x[0] - coef*x[1].
    """
    x = torch.as_tensor(wav, dtype=torch.float64).view(1, 1, -1)
    x = F.pad(x, (1, 0), mode="reflect")
    k = torch.tensor([[[-PRE_EMPH, 1.0]]], dtype=torch.float64)
    return F.conv1d(x, k).view(-1)


def ref_power(wav):
    """`MelSpectrogram(power=2.0)`: |rfft|^2 of the centred STFT.

    torchaudio's defaults matter and are stated rather than assumed:
    `center=True`, `pad_mode='reflect'`, `window_fn=torch.hamming_window`
    (periodic=False), `n_mels` and `f_min`/`f_max` from the reference's
    constructor.
    """
    win = torch.hamming_window(WIN, periodic=False, dtype=torch.float64)
    x = torch.as_tensor(wav, dtype=torch.float64)
    spec = torch.stft(x, n_fft=N_FFT, hop_length=HOP, win_length=WIN,
                      window=win, center=True, pad_mode="reflect",
                      normalized=False, onesided=True, return_complex=True)
    return (spec.real ** 2 + spec.imag ** 2).transpose(0, 1).reshape(-1)


def hz_to_mel(f):
    return 2595.0 * math.log10(1.0 + f / 700.0)


def mel_to_hz(m):
    return 700.0 * (10.0 ** (m / 2595.0) - 1.0)


def ref_filterbank(n_freqs):
    """torchaudio's `melscale_fbanks`, `norm=None`, `mel_scale='htk'`.

    The triangle is drawn in CONTINUOUS Hz and then SAMPLED at the discrete FFT
    bin frequencies -- `linspace(0, sr/2, n_freqs)` -- rather than being floored
    to integer bins and drawn over indices. Those are different filterbanks, and
    the difference is not a rounding detail: band boundaries land between bins,
    so the two disagree by O(1) on many entries.

    Written this way after a first attempt floored the band edges to integer bins
    and reported a total disagreement -- which was the reference being wrong,
    not the port. A reference that has not been shown to be right produces
    exactly this: a confident, entirely fictional finding.

    `norm=None` means no Slaney area normalisation, so peaks stay at 1.0 rather
    than 2/(f_hi - f_lo).
    """
    all_freqs = np.linspace(0.0, SR / 2.0, n_freqs)        # (n_freqs,)
    m_pts = np.linspace(hz_to_mel(F_MIN), hz_to_mel(F_MAX), N_MELS + 2)
    f_pts = mel_to_hz(m_pts)                               # (n_mels + 2,)
    lo = f_pts[0:N_MELS][:, None]
    ctr = f_pts[1:N_MELS + 1][:, None]
    hi = f_pts[2:N_MELS + 2][:, None]
    af = all_freqs[None, :]
    down = (af - lo) / (ctr - lo)                          # rising edge
    up = (hi - af) / (hi - ctr)                             # falling edge
    fb = np.minimum(down, up)
    np.clip(fb, 0.0, None, out=fb)
    return fb


def ref_log_mel(wav):
    """pre-emphasis -> power -> mel -> clamp_min -> log -> per-band mean removal."""
    p = ref_power(ref_pre_emphasis(wav))
    p = torch.as_tensor(p, dtype=torch.float64).view(-1, N_FFT // 2 + 1)
    fb = torch.as_tensor(ref_filterbank(N_FFT // 2 + 1), dtype=torch.float64)
    mel = p @ fb.transpose(0, 1)                       # (frames, mels)
    mel = torch.log(torch.clamp(mel, min=CLAMP))
    return (mel - mel.mean(dim=0, keepdim=True)).transpose(0, 1).reshape(-1)


# ---- parsing and reporting ---------------------------------------------------

def parse(path):
    raw = open(path, "rb").read()
    txt = None
    for enc in ("utf-8", "utf-16", "utf-16-le", "latin-1"):
        cand = raw.decode(enc, errors="replace")
        if "TNORACLE" in cand:
            txt = cand
            break
    if txt is None:
        sys.exit("no TNORACLE marker in the export under any encoding")
    out = {}
    for line in txt.splitlines():
        for key in ("wav", "filtered", "power", "fb", "mel"):
            if line.startswith("TNORACLE " + key + " "):
                out[key] = np.array([float(v) for v in line.split()[2:]],
                                    dtype=np.float64)
    missing = [k for k in ("wav", "filtered", "power", "fb", "mel") if k not in out]
    if missing:
        # A stage that never arrived must be a hard error. Reading it as an empty
        # array and comparing against an empty array passes, which is precisely
        # the shape of "did not check, reported as success".
        sys.exit("export is missing stages: %s" % ", ".join(missing))
    return out


def compare(name, got, ref):
    got = np.asarray(got, dtype=np.float64)
    ref = np.asarray(ref, dtype=np.float64)
    if got.shape != ref.shape:
        print("  %-10s SHAPE port %s reference %s" % (name, got.shape, ref.shape))
        return 1.0
    scale = max(float(np.abs(ref).max()), 1e-30)
    absd = float(np.abs(got - ref).max())
    print("  %-10s n %6d  max|diff| %.4e  relative %.4e  |ref| absmax %.4e"
          % (name, got.size, absd, absd / scale, scale))
    return absd / scale


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


def write_golden(port):
    """Emit the reference for `mbt/tests/wave2/logmel_oracle_test.mbt`."""
    wav = port["wav"]
    filt = ref_pre_emphasis(wav)
    power = ref_power(filt)
    fb = ref_filterbank(N_FFT // 2 + 1)
    mel = ref_log_mel(wav)
    src = f'''// GENERATED by verify/gen_logmel_golden.py. Do not hand-edit: the gate that
// reads these compares against them.
//
// An independent reference for TARNet's acoustic front-end. The whole-model
// autograd gate starts at a log-Mel spectrogram, which leaves everything BEFORE
// it -- pre-emphasis, the STFT, the mel filterbank, the log and the per-band
// mean removal -- verified only by invariants and physics probes. Those cannot
// see a composition that is uniformly wrong.
//
// Independence is the whole point: the STFT here is `torch.stft`, a library
// call, against this package's hand-rolled radix-2 FFT; the window is
// `torch.hamming_window(periodic=False)`; the filterbank is written from the
// HTK definition; pre-emphasis is pad(reflect) + conv1d.
//
// THE FILTERBANK IS THE ONE TO WATCH. Comparing the final log-Mel would have
// MISSED the defect this found: every band peaked at 0.4924 instead of 1.0
// because both triangle edges were divided by the full band width, and
// `log` followed by per-band mean subtraction removes a per-band CONSTANT
// exactly, so the output moved only from 4.9e-03 to 2.5e-03. The filterbank
// itself moved from 5.1e-01 to 1.0e-05. A gate on the output alone would have
// been a gate that could not see the bug it was written for.

///|
///```text
///LOGMEL_N_SAMPLES / LOGMEL_N_FFT / LOGMEL_WIN / LOGMEL_HOP / LOGMEL_N_MELS
///```

///|
///```text
///LOGMEL_N_SAMPLES
///"""

pub const LOGMEL_N_SAMPLES : Int = {wav.size}

///|
///```text
///LOGMEL_N_FFT
///"""

pub const LOGMEL_N_FFT : Int = {N_FFT}

///|
///```text
///LOGMEL_WIN
///"""

pub const LOGMEL_WIN : Int = {WIN}

///|
///```text
///LOGMEL_HOP
///"""

pub const LOGMEL_HOP : Int = {HOP}

///|
///```text
///LOGMEL_N_MELS
///"""

pub const LOGMEL_N_MELS : Int = {N_MELS}

///|
///```text
///logmel_fb_golden : mel filterbank, [n_mels, n_freqs] row-major
///"""

pub let logmel_fb_golden : Array[Float] = {fmt(fb)}

///|
///```text
///logmel_power_golden : power spectrum, [n_frames, n_freqs] row-major
///"""

pub let logmel_power_golden : Array[Float] = {fmt(power)}

///|
///```text
///logmel_mel_golden : final log-Mel, [n_mels, n_frames] row-major
///"""

pub let logmel_mel_golden : Array[Float] = {fmt(mel)}
'''
    out = os.path.join(ROOT, "mbt", "tests", "wave2", "logmel_golden.mbt")
    with io.open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(src)
    print("wrote %s" % out)


def main():
    port = parse(sys.argv[1])
    wav = port["wav"]
    print("log-Mel front-end vs an independent reference")
    print("  n_samples %d  n_fft %d  win %d  hop %d  n_mels %d  pre_emphasis %s"
          % (wav.size, N_FFT, WIN, HOP, N_MELS, PRE_EMPH))
    print()

    worst = 0.0
    filt = ref_pre_emphasis(wav)
    # `power` is computed from the FILTERED signal. Comparing it against a
    # reference built on the raw waveform is a stage-skip, and pre-emphasis is a
    # high-pass with ~1.97x gain at Nyquist, so it reports a ~250% disagreement
    # that is entirely this file's fault. A first draft did exactly that.
    worst = max(worst, compare("wav", wav, wav))
    worst = max(worst, compare("filtered", port["filtered"], filt))
    worst = max(worst, compare("power", port["power"], ref_power(filt)))
    worst = max(worst, compare("fb", port["fb"],
                               ref_filterbank(N_FFT // 2 + 1).reshape(-1)))
    worst = max(worst, compare("mel", port["mel"], ref_log_mel(wav)))
    print()
    print("  worst relative deviation across all four stages: %.4e" % worst)
    if "--write" in sys.argv:
        write_golden(port)


if __name__ == "__main__":
    main()
