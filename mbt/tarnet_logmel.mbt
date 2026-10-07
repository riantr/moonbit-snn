// tarnet_logmel.mbt -- the log-Mel front end TARNet consumes.
//
// Reference: arXiv:2605.07735 (ICME 2026); `AudioDataset._compute_features` in
// github.com/YassinTERRAF/TARNet, `tarnet/datasets/audio_dataset.py`.
//
// The reference builds features under `torch.no_grad()`, so this is
// FORWARD-ONLY BY DESIGN and there is deliberately no backward here. What the
// reference does, in order:
//
//     mel = torchaudio.transforms.MelSpectrogram(
//               sample_rate=16000, n_fft=512, win_length=400, hop_length=160,
//               f_min=20.0, f_max=7600.0, window_fn=torch.hamming_window,
//               n_mels=80)
//     spec = mel(wav) + 1e-6
//     spec = log(spec)
//     spec = spec - spec.mean(dim=-1, keepdim=True)
//
// The `+ 1e-6` comes BEFORE the log, not after: it is a floor on the power
// spectrum so that `log` of a silent frame is finite. Applying it afterwards
// would clamp the log output instead and silently bias loud frames.
//
// torchaudio's unstated defaults matter and are spelled out below rather than
// left as "the default": center=True (reflect padding of n_fft//2 on both
// sides), power=2.0, mel_scale='htk', norm=None, pad_mode='reflect'.

///|
/// Hamming window, symmetric (torch's `periodic=False`, the default when
/// torchaudio calls `window_fn(win_length)`).
///
///     w[n] = 0.54 - 0.46 * cos(2*pi*n / (N-1))
///
/// Note the `N-1`: the periodic variant divides by `N` and is a different
/// function. Getting this wrong changes every frame and is invisible in a
/// spectrogram that still "looks like speech".
pub fn tn_hamming(len : Int) -> Array[Float] {
  let w : Array[Float] = Array::make(len, 0.0F)
  if len == 1 {
    w[0] = 1.0F
    return w
  }
  let d = Float::from_int(len - 1)
  let mut i = 0
  while i < len {
    w[i] = 0.54F - 0.46F * tn_cosf(2.0F * Float::from_int(i) * PI_F / d)
    i = i + 1
  }
  w
}

const PI_F : Float = 3.14159265358979323846F
const LN10_F : Float = 2.302585092994045684F

///|
/// Trigonometry for the window and the FFT. Named `tn_*` rather than `cosf`
/// because `scheduler.mbt` already declares a private `cosf` in this same
/// package, and a second definition of the same name would be a duplicate.
extern "C" fn tn_cosf(x : Float) -> Float = "cosf"

extern "C" fn tn_sinf(x : Float) -> Float = "sinf"

///|
/// base-10 logarithm. `math_native.mbt` exposes only natural `logf`, and
/// dividing by ln(10) is exact enough here: the HTK mel scale multiplies by
/// 2595 afterwards, and a float32 ln(10) is good to 7 digits.
fn tn_log10f(x : Float) -> Float {
  logf(x) / LN10_F
}

///|
/// 10^x, as the inverse of `tn_log10f`.
fn tn_pow10f(x : Float) -> Float {
  expf(x * LN10_F)
}

///|
/// In-place iterative radix-2 Cooley-Tukey FFT.
///
/// `n` must be a power of two; TARNet's `n_fft = 512` is. This is the only FFT
/// in the package, and it is deliberately not the general one -- a mixed-radix
/// or Bluestein implementation would be more general and would silently
/// disagree with this one on non-power-of-two sizes, so the precondition is
/// checked instead of assumed.
fn tn_fft(re : Array[Float], im : Array[Float], inverse : Bool) -> Unit {
  let n = re.length()
  // bit-reversal permutation
  let mut j = 0
  let mut i = 0
  while i < n {
    if i < j {
      let tr = re[i]
      re[i] = re[j]
      re[j] = tr
      let ti = im[i]
      im[i] = im[j]
      im[j] = ti
    }
    let mut bit = n >> 1
    while (j & bit) != 0 {
      j = j ^ bit
      bit = bit >> 1
    }
    j = j | bit
    i = i + 1
  }
  let mut len = 2
  let mut sgn = if inverse { 1.0F } else { -1.0F }
  while len <= n {
    let ang = sgn * 2.0F * PI_F / Float::from_int(len)
    let wl_re = tn_cosf(ang)
    let wl_im = tn_sinf(ang)
    let half = len >> 1
    let mut base = 0
    while base < n {
      let mut cur_re = 1.0F
      let mut cur_im = 0.0F
      let mut k = 0
      while k < half {
        let a = base + k
        let b = a + half
        let u_re = re[a]
        let u_im = im[a]
        let v_re = re[b] * cur_re - im[b] * cur_im
        let v_im = re[b] * cur_im + im[b] * cur_re
        re[a] = u_re + v_re
        im[a] = u_im + v_im
        re[b] = u_re - v_re
        im[b] = u_im - v_im
        let nr = cur_re * wl_re - cur_im * wl_im
        let ni = cur_re * wl_im + cur_im * wl_re
        cur_re = nr
        cur_im = ni
        k = k + 1
      }
      base = base + len
    }
    len = len << 1
  }
  if inverse {
    let inv = 1.0F / Float::from_int(n)
    i = 0
    while i < n {
      re[i] = re[i] * inv
      im[i] = im[i] * inv
      i = i + 1
    }
  }
  sgn = sgn
}

///|
/// HTK mel scale, matching torchaudio's `mel_scale='htk'` default.
fn tn_hz_to_mel(f : Float) -> Float {
  2595.0F * tn_log10f(1.0F + f / 700.0F)
}

fn tn_mel_to_hz(m : Float) -> Float {
  700.0F * (tn_pow10f(m / 2595.0F) - 1.0F)
}

///|
/// `n` evenly spaced values from `lo` to `hi`, INCLUDING both endpoints --
/// torch.linspace semantics. Excluding the upper endpoint would move every
/// filter edge and silently change the bank.
fn tn_linspace(lo : Float, hi : Float, n : Int) -> Array[Float] {
  let out : Array[Float] = Array::make(n, 0.0F)
  if n == 1 {
    out[0] = lo
    return out
  }
  let d = Float::from_int(n - 1)
  let step = (hi - lo) / d
  let mut i = 0
  while i < n {
    out[i] = lo + step * Float::from_int(i)
    i = i + 1
  }
  out
}

///|
/// Triangular mel filterbank, `[n_mels, n_freqs]` row-major. `norm = None`, so
/// the triangles are NOT area-normalised -- matching the reference.
pub fn tn_mel_filterbank(
  n_freqs : Int,
  n_mels : Int,
  sample_rate : Int,
  f_min : Float,
  f_max : Float,
) -> Array[Float] {
  let fb : Array[Float] = Array::make(n_mels * n_freqs, 0.0F)
  let nyq = Float::from_int(sample_rate) / 2.0F
  let all_freqs = tn_linspace(0.0F, nyq, n_freqs)
  let m_min = tn_hz_to_mel(f_min)
  let m_max = tn_hz_to_mel(f_max)
  let m_pts = tn_linspace(m_min, m_max, n_mels + 2)
  let f_pts = Array::make(n_mels + 2, 0.0F)
  let mut i = 0
  while i < n_mels + 2 {
    f_pts[i] = tn_mel_to_hz(m_pts[i])
    i = i + 1
  }
  let mut m = 0
  while m < n_mels {
    let lo = f_pts[m]
    let ctr = f_pts[m + 1]
    let hi = f_pts[m + 2]
    let denom = hi - lo
    let mut k = 0
    while k < n_freqs {
      let f = all_freqs[k]
      let v = if f >= lo && f <= ctr {
        if denom > 0.0F { (f - lo) / denom } else { 0.0F }
      } else if f > ctr && f <= hi {
        if denom > 0.0F { (hi - f) / denom } else { 0.0F }
      } else {
        0.0F
      }
      fb[m * n_freqs + k] = v
      k = k + 1
    }
    m = m + 1
  }
  fb
}

///|
/// Reflect-pad `wav` by `pad` on both sides (torch `mode='reflect'`), which
/// EXCLUDES the edge sample: the first padded value is `wav[pad]`, not
/// `wav[pad - 1]`. Getting that off-by-one wrong changes only the first and
/// last frame, which is exactly where a spectrogram is least inspected.
fn tn_reflect_pad(wav : Array[Float], pad : Int) -> Array[Float] {
  let l = wav.length()
  let out : Array[Float] = Array::make(l + 2 * pad, 0.0F)
  let mut i = 0
  while i < l {
    out[pad + i] = wav[i]
    i = i + 1
  }
  i = 0
  while i < pad {
    out[pad - 1 - i] = wav[i + 1]
    i = i + 1
  }
  i = 0
  while i < pad {
    out[pad + l + i] = wav[l - 2 - i]
    i = i + 1
  }
  out
}

///|
/// Power spectrum, `[n_frames, n_freqs]` row-major, `power = 2.0`.
pub fn tn_stft_power(
  wav : Array[Float],
  n_fft : Int,
  win_length : Int,
  hop : Int,
) -> Array[Float] {
  let l = wav.length()
  let padded = tn_reflect_pad(wav, n_fft >> 1)
  // window of win_length centred inside n_fft
  let win = tn_hamming(win_length)
  let off = (n_fft - win_length) >> 1
  let n_frames = 1 + (padded.length() - n_fft) / hop
  let n_freqs = n_fft / 2 + 1
  let out : Array[Float] = Array::make(n_frames * n_freqs, 0.0F)
  let re : Array[Float] = Array::make(n_fft, 0.0F)
  let im : Array[Float] = Array::make(n_fft, 0.0F)
  let mut fr = 0
  while fr < n_frames {
    let base = fr * hop
    let mut i = 0
    while i < n_fft {
      let w = if i >= off && i < off + win_length {
        win[i - off]
      } else {
        0.0F
      }
      re[i] = padded[base + i] * w
      im[i] = 0.0F
      i = i + 1
    }
    tn_fft(re, im, false)
    let mut k = 0
    while k < n_freqs {
      out[fr * n_freqs + k] = re[k] * re[k] + im[k] * im[k]
      k = k + 1
    }
    fr = fr + 1
  }
  out
}

///|
/// Complex FFT of a real signal, zero-padded up to the next power of two.
/// Returns `(re, im)` of length `n`.
///
/// Public so the gate can compare the transform against `tn_dft` in PHASE as
/// well as magnitude. Magnitude alone cannot distinguish `X[k]` from its
/// conjugate, and that conjugate is exactly what a sign-flipped twiddle factor
/// produces -- a bug that passes every magnitude check and inverts the
/// spectrum.
pub fn tn_fft_complex(x : Array[Float]) -> (Array[Float], Array[Float]) {
  let mut n = 1
  while n < x.length() {
    n = n << 1
  }
  let re : Array[Float] = Array::make(n, 0.0F)
  let im : Array[Float] = Array::make(n, 0.0F)
  let mut i = 0
  while i < x.length() {
    re[i] = x[i]
    i = i + 1
  }
  tn_fft(re, im, false)
  (re, im)
}

///|
/// Magnitude spectrum of a real signal, `[n/2 + 1]` bins, where `n` is `x`
/// rounded up to a power of two.
fn tn_fft_magnitude(x : Array[Float]) -> Array[Float] {
  let (re, im) = tn_fft_complex(x)
  let bins = re.length() / 2 + 1
  let out : Array[Float] = Array::make(bins, 0.0F)
  let mut i = 0
  while i < bins {
    out[i] = sqrtf(re[i] * re[i] + im[i] * im[i])
    i = i + 1
  }
  out
}

///|
/// Direct O(n^2) DFT, kept as the FFT's oracle for the gate. Not used on any
/// real path: it is O(n^2) and exists only so the fast transform can be
/// checked against the definition rather than against another fast transform.
pub fn tn_dft(re : Array[Float]) -> (Array[Float], Array[Float]) {
  let n = re.length()
  let or_ : Array[Float] = Array::make(n, 0.0F)
  let oi : Array[Float] = Array::make(n, 0.0F)
  let two_pi = 2.0F * PI_F
  let mut k = 0
  while k < n {
    let mut sr = 0.0F
    let mut si = 0.0F
    let mut t = 0
    while t < n {
      let ang = -two_pi * Float::from_int(k * t) / Float::from_int(n)
      sr = sr + re[t] * tn_cosf(ang)
      si = si + re[t] * tn_sinf(ang)
      t = t + 1
    }
    or_[k] = sr
    oi[k] = si
    k = k + 1
  }
  (or_, oi)
}

///|
/// Pre-emphasis: a first-order high-pass applied to the WAVEFORM, before the
/// STFT. This is part of the reference's model front end, not an optional
/// extra, and it was MISSING from the first version of this file.
///
/// The reference builds it as
///
///     self.flipped_filter = tensor([[-coef, 1.0]]).unsqueeze(1)
///     x = F.pad(x, (1, 0), mode="reflect")     # one sample, on the LEFT
///     x = F.conv1d(x, self.flipped_filter)
///
/// which is length-preserving and equals
///
///     y[0] = x[0] - coef * x[1]
///     y[n] = x[n] - coef * x[n - 1]     for n >= 1
///
/// Note the reflected FIRST sample: it borrows `x[1]`, not `x[0]`. Reflect
/// padding excludes the edge sample, so writing `y[0] = x[0] - coef * x[0]`
/// would be a different filter -- and it would also destroy the signal, since
/// that is `(1 - coef) * x[0]` = 0.03 * x[0].
///
/// `coef <= 0` disables the filter, which is what the reference's DATASET path
/// does (see `tn_log_mel`).
pub fn tn_pre_emphasis(wav : Array[Float], coef~ : Float = 0.97F) -> Array[Float] {
  let n = wav.length()
  if coef <= 0.0F || n < 2 {
    return wav.copy()
  }
  let out : Array[Float] = Array::make(n, 0.0F)
  // reflect-pad one sample on the left: the pad borrows x[1]
  let xpad = Array::make(n + 1, 0.0F)
  xpad[0] = wav[1]
  let mut i = 1
  while i <= n {
    xpad[i] = wav[i - 1]
    i = i + 1
  }
  // conv1d with kernel [-coef, 1.0]: y[n] = -coef * xpad[n] + 1.0 * xpad[n+1]
  i = 0
  while i < n {
    out[i] = xpad[i + 1] - coef * xpad[i]
    i = i + 1
  }
  out
}

///|
/// The reference's whole feature pipeline, for one waveform.
///
/// Returns `[n_mels, n_frames]` row-major, which is `[1, in_chan, t]` to
/// `tarnet_forward` for a batch of one.
///
/// `wav` MUST already be mono at 16 kHz. The reference resamples with
/// torchaudio's Kaiser-windowed sinc kernel, which is not reproducible here;
/// resampling is therefore a precondition, not a step, and is stated rather
/// than approximated -- a silently-wrong resample produces plausible features.
///
/// TWO FRONT ENDS EXIST IN THE REFERENCE, and they are not the same:
///
///   - the MODEL path, `backbone.forward`: `PreEmphasis(0.97)` -> MelSpectrogram
///     -> `clamp_min(1e-6)` -> log -> per-band mean removal. This is the path
///     training actually runs: `train_voxceleb.py` defaults `feature_type` to
///     `'raw'`, so the dataset hands the model a WAVEFORM and the model does its
///     own feature extraction. `pre_emphasis` defaults to 0.97 to match.
///   - the DATASET path, `AudioDataset._compute_features`: the same but with
///     NO pre-emphasis. Pass `pre_emphasis = 0.0F` for that variant.
///
/// `clamp_min(1e-6)` versus `+ 1e-6` is not a behavioural difference here:
/// both floor the power spectrum, and after the per-band mean removal a uniform
/// additive shift cancels exactly, so the two agree except where the clamp is
/// genuinely active.
pub fn tn_log_mel(
  wav : Array[Float],
  sample_rate~ : Int = 16000,
  n_fft~ : Int = 512,
  win_length~ : Int = 400,
  hop~ : Int = 160,
  n_mels~ : Int = 80,
  f_min~ : Float = 20.0F,
  f_max~ : Float = 7600.0F,
  pre_emphasis~ : Float = 0.97F,
) -> Array[Float] {
  let filtered = tn_pre_emphasis(wav, coef=pre_emphasis)
  let power = tn_stft_power(filtered, n_fft, win_length, hop)
  let n_freqs = n_fft / 2 + 1
  let n_frames = power.length() / n_freqs
  let fb = tn_mel_filterbank(n_freqs, n_mels, sample_rate, f_min, f_max)
  let mel : Array[Float] = Array::make(n_mels * n_frames, 0.0F)
  let mut m = 0
  while m < n_mels {
    let mut t = 0
    while t < n_frames {
      let mut acc = 0.0F
      let mut k = 0
      while k < n_freqs {
        let w = fb[m * n_freqs + k]
        if w != 0.0F {
          acc = acc + w * power[t * n_freqs + k]
        }
        k = k + 1
      }
      mel[m * n_frames + t] = acc
      t = t + 1
    }
    m = m + 1
  }
  // +1e-6 BEFORE the log: a floor on the power, not a clamp on the log.
  let mut i = 0
  while i < mel.length() {
    mel[i] = logf(mel[i] + 1.0e-6F)
    i = i + 1
  }
  // mean-normalise across time, per mel band
  m = 0
  while m < n_mels {
    let mut s = 0.0F
    let mut t = 0
    while t < n_frames {
      s = s + mel[m * n_frames + t]
      t = t + 1
    }
    let mean = s / Float::from_int(n_frames)
    t = 0
    while t < n_frames {
      mel[m * n_frames + t] = mel[m * n_frames + t] - mean
      t = t + 1
    }
    m = m + 1
  }
  mel
}

///|
/// Number of frames `tn_log_mel` produces for a waveform of `wav_len` samples.
/// Exposed so a caller can size its buffer instead of guessing.
pub fn tn_log_mel_frames(wav_len : Int, n_fft~ : Int = 512, hop~ : Int = 160) -> Int {
  1 + (wav_len + 2 * (n_fft >> 1) - n_fft) / hop
}

///|
/// Stack per-utterance `[n_mels, n_frames]` features into one
/// `[n, in_chan, t]` batch, zero-padding `t` to the longest utterance.
///
/// Zero-pad rather than truncate: the reference's layer norms hold a scale and
/// shift per frame, so the frame count is baked into the model, and silently
/// dropping the tail of the longest utterance would train on less audio than
/// the loss claims.
pub fn tn_log_mel_batch(
  feats : Array[Array[Float]],
  n_mels : Int,
) -> Array[Float] {
  let n = feats.length()
  let mut t_max = 0
  let mut i = 0
  while i < n {
    if feats[i].length() / n_mels > t_max {
      t_max = feats[i].length() / n_mels
    }
    i = i + 1
  }
  let out : Array[Float] = Array::make(n * n_mels * t_max, 0.0F)
  i = 0
  while i < n {
    let t = feats[i].length() / n_mels
    let mut m = 0
    while m < n_mels {
      let mut t_i = 0
      while t_i < t {
        out[(i * n_mels + m) * t_max + t_i] = feats[i][m * t + t_i]
        t_i = t_i + 1
      }
      m = m + 1
    }
    i = i + 1
  }
  out
}

///|
/// Frame count of the padded batch that `tn_log_mel_batch` produces.
pub fn tn_log_mel_batch_frames(feats : Array[Array[Float]], n_mels : Int) -> Int {
  let mut t_max = 0
  let mut i = 0
  while i < feats.length() {
    if feats[i].length() / n_mels > t_max {
      t_max = feats[i].length() / n_mels
    }
    i = i + 1
  }
  t_max
}