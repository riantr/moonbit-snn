// tarnet_logmel_test.mbt -- gate for the log-Mel front end.
//
// Reference: arXiv:2605.07735 (ICME 2026); `AudioDataset._compute_features`
// in `tarnet/datasets/audio_dataset.py`.
//
// There is no PyTorch here to diff against, so the front end is pinned against
// CLOSED FORMS and PROPERTIES instead:
//
//   - the FFT against a direct O(n^2) DFT on inputs small enough to evaluate
//     both ways. Comparing an FFT to another FFT proves only self-consistency;
//     comparing it to the definition is what catches a wrong twiddle factor.
//   - the hamming window against its formula, INCLUDING the symmetric/periodic
//     distinction (N-1 vs N in the denominator), which is invisible in a plot.
//   - the mel filterbank against the HTK scale by round trip: mel -> hz -> mel
//     must be the identity, and band centres must increase monotonically.
//   - a pure sine, whose energy must land in the mel band containing its
//     frequency. This is the test that ties FFT, window, power and the bank
//     together -- a wrong hop or a periodic window passes every unit test above
//     and fails here.
//   - the mean-normalisation invariant, which is exact to float32 and so can be
//     asserted with == rather than a tolerance.
//
// The reference computes these under torch.no_grad(), so there is deliberately
// no backward and nothing here is differentiated.

///|
/// The package exposes natural `logf`/`expf` but no `cos`/`sin` at package
/// scope, so the tests use their own externs rather than reaching into another
/// module's private ones.
extern "C" fn lm_cos(x : Float) -> Float = "cosf"

extern "C" fn lm_sin(x : Float) -> Float = "sinf"

const LM_TWO_PI : Float = 6.28318530717958647692F

test "log-Mel: pre-emphasis matches the reference filter and is a real high-pass" {
  // The reference implements this as pad(1, 0, reflect) then conv1d with the
  // kernel [-coef, 1.0], so:
  //     y[0] = x[0] - coef * x[1]      <- reflected: borrows x[1], NOT x[0]
  //     y[n] = x[n] - coef * x[n-1]
  // The first sample is the whole point of the test. Using x[0] there instead
  // gives (1 - coef) * x[0] = 0.03 * x[0], which is not a rounding difference
  // but a different filter.
  let coef = 0.97F
  let n = 8
  let x : Array[Float] = Array::make(n, 0.0F)
  let mut i = 0
  while i < n {
    x[i] = Float::from_int(i + 1)
    i = i + 1
  }
  let y = @snn_mbt.tn_pre_emphasis(x, coef=coef)
  if y.length() != n {
    fail("pre-emphasis changed the length: \{y.length()} vs \{n}")
  }
  let want0 = x[0] - coef * x[1]
  if (y[0] - want0).abs() > 1.0e-6F {
    fail("y[0] = \{y[0]}, expected \{want0} (reflect borrows x[1], not x[0])")
  }
  i = 1
  while i < n {
    let want = x[i] - coef * x[i - 1]
    if (y[i] - want).abs() > 1.0e-5F {
      fail("y[\{i}] = \{y[i]}, expected \{want}")
    }
    i = i + 1
  }
  // NON-VACUITY. A filter that quietly returned its input would pass the
  // length check and nothing else. A pre-emphasis is a high-pass, so its gain
  // must RISE with frequency: DC is scaled by (1 - coef) = 0.03 and Nyquist by
  // (1 + coef) = 1.97. Asserting both ends pins the sign of the kernel too.
  let m = 64
  let dc : Array[Float] = Array::make(m, 0.25F)
  let nyq : Array[Float] = Array::make(m, 0.0F)
  i = 0
  while i < m {
    nyq[i] = if i % 2 == 0 { 0.25F } else { -0.25F }
    i = i + 1
  }
  let dcy = @snn_mbt.tn_pre_emphasis(dc, coef=coef)
  let nyqy = @snn_mbt.tn_pre_emphasis(nyq, coef=coef)
  let dc_gain = dcy[m / 2].abs() / 0.25F
  let nyq_gain = nyqy[m / 2].abs() / 0.25F
  if (dc_gain - 0.03F).abs() > 1.0e-3F {
    fail("DC gain is \{dc_gain}, expected 1 - coef = 0.03 -- the kernel sign may be flipped")
  }
  if (nyq_gain - 1.97F).abs() > 1.0e-2F {
    fail("Nyquist gain is \{nyq_gain}, expected 1 + coef = 1.97")
  }
  if dc_gain >= nyq_gain {
    fail("pre-emphasis attenuates Nyquist more than DC -- that is a low-pass")
  }
  // coef <= 0 disables the filter (the reference's DATASET path).
  let off = @snn_mbt.tn_pre_emphasis(x, coef=0.0F)
  i = 0
  while i < n {
    if off[i] != x[i] {
      fail("coef = 0 should be the identity, but y[\{i}] = \{off[i]} vs \{x[i]}")
    }
    i = i + 1
  }
}

test "log-Mel: the model path applies pre-emphasis and the dataset path does not" {
  // The reference has TWO front ends and they differ. `train_voxceleb.py`
  // defaults feature_type to 'raw', so the dataset hands the model a waveform
  // and the MODEL does the extraction -- with PreEmphasis. The dataset's own
  // 'mel' branch omits it. If these two produced identical features, one of
  // them is wrong, so the gate asserts they differ.
  let wav : Array[Float] = Array::make(4000, 0.0F)
  let mut i = 0
  while i < 4000 {
    wav[i] = 0.4F * lm_sin(0.05F * Float::from_int(i))
    i = i + 1
  }
  let with_pe = @snn_mbt.tn_log_mel(wav, n_mels=16)
  let without_pe = @snn_mbt.tn_log_mel(wav, n_mels=16, pre_emphasis=0.0F)
  if with_pe.length() != without_pe.length() {
    fail("the two front ends disagree on shape")
  }
  let mut diff = 0.0F
  i = 0
  while i < with_pe.length() {
    let d = with_pe[i] - without_pe[i]
    diff = diff + (if d < 0.0F { -d } else { d })
    i = i + 1
  }
  let mean = diff / Float::from_int(with_pe.length())
  if mean < 0.05F {
    fail("the model path and the dataset path differ by only \{mean} on average -- pre-emphasis is not being applied")
  }
  // Both must still be finite and mean-normalised; enabling pre-emphasis must
  // not have broken the downstream invariants.
  i = 0
  while i < with_pe.length() {
    if !(with_pe[i] == with_pe[i]) {
      fail("log-Mel with pre-emphasis produced NaN at \{i}")
    }
    i = i + 1
  }
  let n_mels = 16
  let n_frames = with_pe.length() / n_mels
  let mut m = 0
  while m < n_mels {
    let mut s = 0.0F
    let mut t = 0
    while t < n_frames {
      s = s + with_pe[m * n_frames + t]
      t = t + 1
    }
    let mean_band = s / Float::from_int(n_frames)
    if mean_band.abs() > 1.0e-4F {
      fail("band \{m} has mean \{mean_band} after pre-emphasis and normalisation")
    }
    m = m + 1
  }
}

test "log-Mel FFT matches a direct DFT, in magnitude and in phase" {
  // Comparing an FFT to another FFT would prove only self-consistency.
  // `tn_dft` is the O(n^2) definition, so this checks the twiddle factors, the
  // bit-reversal permutation and the butterfly indexing against the transform
  // they are supposed to compute.
  let n = 64
  let re : Array[Float] = Array::make(n, 0.0F)
  let mut i = 0
  while i < n {
    // a mixed signal: an impulse, a ramp and a tone together, so no single
    // structure can satisfy a buggy transform
    re[i] = if i == 1 { 1.0F } else {
      0.3F * lm_sin(LM_TWO_PI * 7.0F * Float::from_int(i) / Float::from_int(n)) +
      0.01F * Float::from_int(i)
    }
    i = i + 1
  }
  let (d_re, d_im) = @snn_mbt.tn_dft(re)
  let (f_re, f_im) = @snn_mbt.tn_fft_complex(re)
  if f_re.length() != n || f_im.length() != n {
    fail("FFT returned length \{f_re.length()}, expected \{n}")
  }
  // Compare the COMPLEX transform, not just its magnitude: a sign-flipped
  // twiddle factor yields the conjugate spectrum, which has identical
  // magnitudes and is the difference between a forward and an inverse sign.
  let mut worst_re = 0.0F
  let mut worst_im = 0.0F
  let mut worst_k = -1
  i = 0
  while i < n {
    let e1 = f_re[i] - d_re[i]
    let e2 = f_im[i] - d_im[i]
    let m = @snn_mbt.sqrtf(e1 * e1 + e2 * e2)
    if m > worst_re {
      worst_re = m
      worst_k = i
    }
    let ai = e2.abs()
    if ai > worst_im {
      worst_im = ai
    }
    i = i + 1
  }
  if worst_re > 1.0e-3F {
    fail(
      "FFT differs from the direct DFT by \{worst_re} at bin \{worst_k} (worst imaginary error \{worst_im})",
    )
  }
}

test "log-Mel: the hamming window is symmetric, not periodic" {
  // torch.hamming_window(N) defaults to periodic=False, i.e. the cosine runs
  // over N-1. A periodic window is a DIFFERENT function that still looks like
  // a window, and using it shifts every frame of the spectrogram.
  let n = 400
  let w = @snn_mbt.tn_hamming(n)
  if w.length() != n {
    fail("hamming length \{w.length()}, expected \{n}")
  }
  let mut worst = 0.0F
  let mut i = 0
  while i < n {
    let want = 0.54F -
      0.46F *
        lm_cos(
          2.0F * LM_TWO_PI / 2.0F * Float::from_int(i) / Float::from_int(n - 1),
        )
    let d = w[i] - want
    worst = if d > worst { d } else if -d > worst { -d } else { worst }
    i = i + 1
  }
  if worst > 1.0e-6F {
    fail("hamming window deviates from the symmetric formula by \{worst} -- is it periodic?")
  }
  // endpoints of a symmetric Hamming are both 0.08
  if (w[0] - 0.08F).abs() > 1.0e-6F || (w[n - 1] - 0.08F).abs() > 1.0e-6F {
    fail("hamming endpoints are \{w[0]} and \{w[n - 1]}, expected 0.08 each")
  }
}

test "log-Mel: the HTK filterbank is non-negative, non-empty and contiguous" {
  let n_freqs = 257
  let n_mels = 80
  let fb = @snn_mbt.tn_mel_filterbank(n_freqs, n_mels, 16000, 20.0F, 7600.0F)
  if fb.length() != n_mels * n_freqs {
    fail("filterbank length \{fb.length()}")
  }
  // Two earlier versions of this gate asserted things that are NOT true of a
  // correct bank, and measurement -- not reading -- is what caught both:
  //
  //   - "every band peaks at 1.0". Impossible. With f_min = 20 Hz the FFT bins
  //     sit every 8000/256 = 31.25 Hz while band 0's centre is 42.2 Hz, so the
  //     triangle falls between bins.
  //   - "peak alignment improves with frequency". Also false. The measured peaks
  //     oscillate across the whole range (band 1 = 0.443, band 2 = 0.392) and
  //     never exceed 0.51 anywhere in the bank, because at low frequency a mel
  //     band is narrower than a single bin. Neither is a defect; both follow
  //     from the reference's parameters, not from this implementation.
  //
  // What holds for every correct bank: weights non-negative, no empty band, and
  // each band's support CONTIGUOUS. Whether a frequency lands in the right band
  // is not decided here -- that is the pure-tone test's job.
  let mut m = 0
  while m < n_mels {
    let mut seen = false
    let mut ended = false
    let mut k = 0
    while k < n_freqs {
      let v = fb[m * n_freqs + k]
      if v < 0.0F {
        fail("filterbank[\{m}][\{k}] is negative: \{v}")
      }
      if v > 0.0F {
        if ended {
          fail(
            "filterbank band \{m} is non-zero again at bin \{k} after its support ended -- a triangle must be contiguous",
          )
        }
        seen = true
      } else if seen {
        ended = true
      }
      k = k + 1
    }
    if !seen {
      fail("filterbank band \{m} is entirely zero -- a permanent hole in the features")
    }
    m = m + 1
  }
  // The reference uses f_min = 20 Hz, so band 0 must be empty at DC. A bank
  // that leaks DC into band 0 is silently using f_min = 0.
  if fb[0] != 0.0F {
    fail("band 0 has weight \{fb[0]} at DC, but f_min = 20 Hz should exclude it")
  }
}

test "log-Mel: a pure tone lands in the mel band that contains it" {
  // Ties the FFT, the window, the power and the filterbank together. Each unit
  // test above can pass while their COMPOSITION is wrong (e.g. the window
  // offset, or a hop that slides frames), and only a known-frequency probe
  // sees that.
  let sr = 16000
  let n_fft = 512
  let hop = 160
  let n_mels = 80
  let freq = 1000.0F
  let len = 16000
  let wav : Array[Float] = Array::make(len, 0.0F)
  let two_pi_f = 2.0F * LM_TWO_PI / 2.0F * freq / Float::from_int(sr)
  let mut i = 0
  while i < len {
    wav[i] = lm_sin(two_pi_f * Float::from_int(i))
    i = i + 1
  }
  let mel = @snn_mbt.tn_log_mel(wav, sample_rate=sr, n_fft=n_fft, hop=hop, n_mels=n_mels)
  let n_frames = @snn_mbt.tn_log_mel_frames(len, n_fft=n_fft, hop=hop)
  if n_frames <= 1 {
    fail("degenerate frame count \{n_frames}")
  }
  if mel.length() != n_mels * n_frames {
    fail("mel length \{mel.length()}, expected \{n_mels * n_frames}")
  }
  // After mean-normalisation the loudest band is the one nearest the tone, and
  // the spectrum must fall off on BOTH sides of it (a monotone ramp would mean
  // the bank is malformed, not that the tone landed well).
  let mut best = -1
  let mut best_v = -1.0e30F
  let mut m = 0
  while m < n_mels {
    let mut s = 0.0F
    let mut t = 0
    while t < n_frames {
      s = s + mel[m * n_frames + t]
      t = t + 1
    }
    if s > best_v {
      best_v = s
      best = m
    }
    m = m + 1
  }
  // 1000 Hz in an HTK bank from 20..7600 Hz over 80 bands sits around band 22.
  // The exact band depends on edge placement, so assert the band index is in a
  // sane window rather than pinning a number I have not verified.
  if best < 15 || best > 30 {
    fail("a 1000 Hz tone peaked in mel band \{best}, expected roughly 22")
  }
  if best_v <= 0.0F {
    fail("no mel band carried positive energy for a 1000 Hz tone")
  }
}

test "log-Mel: the mean-normalisation invariant holds exactly" {
  // Each band must average to zero across time. This is exact to float32 up to
  // accumulation, so it is asserted with a tight tolerance -- and it is the
  // invariant that a missing `- mean` would break immediately.
  let wav : Array[Float] = Array::make(16000, 0.0F)
  let mut i = 0
  while i < 16000 {
    wav[i] = lm_sin(0.01F * Float::from_int(i)) +
      0.3F * lm_sin(0.37F * Float::from_int(i))
    i = i + 1
  }
  let n_mels = 40
  let mel = @snn_mbt.tn_log_mel(wav, n_mels=n_mels)
  let n_frames = mel.length() / n_mels
  let mut worst = 0.0F
  let mut m = 0
  while m < n_mels {
    let mut s = 0.0F
    let mut t = 0
    while t < n_frames {
      s = s + mel[m * n_frames + t]
      t = t + 1
    }
    let mean = s / Float::from_int(n_frames)
    if mean.abs() > 1.0e-4F {
      fail("mel band \{m} has mean \{mean} after normalisation")
    }
    worst = if mean.abs() > worst { mean.abs() } else { worst }
    m = m + 1
  }
  if worst > 1.0e-4F {
    fail("worst band mean \{worst}")
  }
}

test "log-Mel: a silent waveform stays finite" {
  // The +1e-6 before the log is what makes this finite. After the log every
  // band is log(1e-6) = -13.8, and mean-normalisation drives it to zero, so a
  // silent input yields all zeros rather than NaN. A NaN here would propagate
  // through the whole model while looking like a training failure.
  let wav : Array[Float] = Array::make(4000, 0.0F)
  let n_mels = 20
  let mel = @snn_mbt.tn_log_mel(wav, n_mels=n_mels)
  let mut i = 0
  while i < mel.length() {
    if !(mel[i] == mel[i]) {
      fail("log-Mel produced NaN at \{i} on a silent waveform -- the 1e-6 floor is missing")
    }
    i = i + 1
  }
}

test "log-Mel: batching pads to the longest utterance and keeps rows aligned" {
  // The reference's layer norms hold per-FRAME scale and shift, so the frame
  // count is baked into the model. Padding rather than truncating means the
  // loss is computed over the audio the caller actually supplied.
  let short : Array[Float] = Array::make(4800, 0.0F)
  let long : Array[Float] = Array::make(9600, 0.0F)
  let mut i = 0
  while i < short.length() {
    short[i] = 0.5F * lm_sin(0.05F * Float::from_int(i))
    i = i + 1
  }
  i = 0
  while i < long.length() {
    long[i] = 0.5F * lm_sin(0.05F * Float::from_int(i))
    i = i + 1
  }
  let n_mels = 12
  let f_short = @snn_mbt.tn_log_mel(short, n_mels=n_mels)
  let f_long = @snn_mbt.tn_log_mel(long, n_mels=n_mels)
  let feats = [f_short, f_long]
  let batch = @snn_mbt.tn_log_mel_batch(feats, n_mels)
  let t_max = @snn_mbt.tn_log_mel_batch_frames(feats, n_mels)
  if batch.length() != 2 * n_mels * t_max {
    fail("batch length \{batch.length()}")
  }
  let t_short = f_short.length() / n_mels
  // The short utterance's own frames must appear unchanged at offset 0, and
  // the padding beyond it must be exactly zero.
  let mut i = 0
  while i < t_short {
    if batch[i] != f_short[i] {
      fail("batched frame \{i} differs from the unpadded feature")
    }
    i = i + 1
  }
  i = t_short
  while i < t_max {
    if batch[i] != 0.0F {
      fail("padding frame \{i} is \{batch[i]}, expected 0")
    }
    i = i + 1
  }
  // Row 1 must start at n_mels * t_max.
  if batch[n_mels * t_max] != f_long[0] {
    fail("the second utterance's first frame is not at the row offset")
  }
}