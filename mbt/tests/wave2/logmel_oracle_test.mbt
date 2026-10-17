// logmel_oracle_test.mbt -- TARNet's front-end against an independent reference.
//
// WHY THIS GATE AND NOT JUST THE INVARIANT SUITE. `tarnet_logmel_test.mbt` is a
// good suite and every check in it is worth having: the FFT against a direct
// DFT, a pure tone against the mel band that contains it, the window's
// symmetric-vs-periodic distinction, the filterbank's non-negativity and
// contiguity. None of them can see a composition that is uniformly wrong.
//
// It did not see this one. `tn_mel_filterbank` divided BOTH triangle edges by
// `hi - lo` instead of by `ctr - lo` and `hi - ctr`, so every band's peak came
// out at 0.4924 rather than 1.0 -- and a triangle scaled by 0.49 is still
// non-negative, still contiguous, still excludes DC at band 0. All three
// properties held.
//
// WORTH READING TWICE: THE OUTPUT GATE CANNOT SEE IT. Because the error is a
// per-band CONSTANT, and `log` followed by per-band mean removal removes a
// per-band constant exactly,
//
//     log(s * E)[t] - mean_t log(s * E)  ==  log E[t] - mean_t log E
//
// the final log-Mel moved only from 4.9e-03 to 2.5e-03 when the bug was fixed.
// A gate on the output alone would have been a gate that could not see the bug
// it was written for. The FILTERBANK moved from 5.1e-01 to 1.0e-05, a factor of
// 50000. So `fb` is the gate, and `mel` is reported beside it to show how
// little the output moved -- which is the finding, not a footnote.
//
// Tolerances are measured, not fitted:
//   fb     worst relative 1.03e-05 -> rtol 1e-03, ~100x margin
//   power  worst relative 1.84e-06 -> rtol 1e-04, ~50x margin
//   mel    worst relative 2.49e-03 -> rtol 1e-02, ~4x margin, reported only
// The defect misses the fb bound by a factor of ~490. The mel bound would have
// been cleared by the broken implementation.

///|
/// The same integer-hash waveform `logmel_export_test.mbt` exports. Identical
/// fixtures matter: a reference computed from a different waveform agrees about
/// nothing, and would report a disagreement rather than a defect.
fn ln_fixture(n : Int) -> Array[Float] {
  let w : Array[Float] = Array::make(n, 0.0F)
  let mut i = 0
  while i < n {
    w[i] = Float::from_int((i * 37 + 11) % 101 - 50) / 50.0F
    i = i + 1
  }
  w
}

///|
/// Count the coordinates outside `atol + rtol*|golden|`, plus the worst relative
/// excess and where it is. A count alone cannot tell one systematic fault from
/// twenty scattered ones.
fn ln_count_over(
  got : Array[Float],
  want : Array[Float],
  atol : Float,
  rtol : Float,
) -> (Int, Float, Int) {
  let mut over = 0
  let mut worst = 0.0F
  let mut worst_i = -1
  let mut i = 0
  while i < want.length() {
    let w = want[i]
    let aw = if w < 0.0F { -w } else { w }
    let d = got[i] - w
    let ad = if d < 0.0F { -d } else { d }
    if ad - atol - rtol * aw > 0.0F {
      over = over + 1
      let rel = (ad - atol) / aw
      if aw > 0.0F && rel > worst {
        worst = rel
        worst_i = i
      }
    }
    i = i + 1
  }
  (over, worst, worst_i)
}

///|
/// Check one stage against the golden and return a failure message, or "" when
/// it passes. Returning the message rather than calling `fail` keeps `fail` in
/// the test bodies: MoonBit only allows it in a function with an error type.
/// `strict = false` reports without gating, which is what L3 needs.
fn ln_check(
  label : String,
  got : Array[Float],
  want : Array[Float],
  atol : Float,
  rtol : Float,
  strict : Bool,
) -> String {
  if got.length() != want.length() {
    return label + ": port has " + got.length().to_string() + " values, the golden has " + want.length().to_string()
  }
  let (over, worst, worst_i) = ln_count_over(got, want, atol, rtol)
  println(label + ": n " + got.length().to_string() + "  over " + over.to_string() + "  worst relative " + worst.to_string() + " at [" + worst_i.to_string() + "]")
  if strict && over > 0 {
    return label + ": " + over.to_string() + " of " + want.length().to_string() + " coordinates are outside the bound; worst relative excess " + worst.to_string() + " at [" + worst_i.to_string() + "]"
  }
  ""
}

test "L1: the mel filterbank matches an independent reference" {
  let n_freqs = LOGMEL_N_FFT / 2 + 1
  let fb = @tarnet.tn_mel_filterbank(n_freqs, LOGMEL_N_MELS, 16000, 20.0F, 7600.0F)
  let m = ln_check("L1 filterbank", fb, logmel_fb_golden, 1.0e-5F, 1.0e-3F, true)
  if m != "" {
    fail(m)
  }
}

test "L2: the power spectrum matches torch.stft" {
  let wav = ln_fixture(LOGMEL_N_SAMPLES)
  let filtered = @tarnet.tn_pre_emphasis(wav, coef=0.97F)
  let power = @tarnet.tn_stft_power(filtered, LOGMEL_N_FFT, LOGMEL_WIN, LOGMEL_HOP)
  let m = ln_check("L2 power", power, logmel_power_golden, 1.0e-2F, 1.0e-4F, true)
  if m != "" {
    fail(m)
  }
}

test "L3: the log-Mel output matches -- reported, NOT gated" {
  // Reported on purpose. The filterbank defect this front-end had moved this
  // quantity by 4.9e-03, and it is reported here as 2.5e-03, so a bound tight
  // enough to catch the defect is tighter than float32 across a 257-term dot
  // product and a logarithm. Asserting it would be asserting something that
  // does not hold, and L1 is the gate that does.
  let wav = ln_fixture(LOGMEL_N_SAMPLES)
  let mel = @tarnet.tn_log_mel(wav)
  ignore(ln_check("L3 log-Mel (reported)", mel, logmel_mel_golden, 1.0e-1F, 1.0e-2F, false))
}

test "L4: no naive peak invariant exists here, and that is the point" {
  // There is deliberately NO assertion in this test, and the reason is the
  // finding rather than an omission.
  //
  // The obvious cheap guard for this bug is "every band peaks at 1.0". It is
  // FALSE. The triangle is continuous in Hz and SAMPLED at the 257 FFT bin
  // frequencies, 31.25 Hz apart, so a band peaks at 1.0 only when a bin happens
  // to land on its centre. Measured on this bank:
  //
  //     band  3   centre  112.4 Hz   nearest bin 125.0 Hz (12.6 away)  peak 0.4934
  //     band 20   centre  657.1 Hz   nearest bin 656.2 Hz ( 0.8 away)  peak 0.9799
  //     band 40   centre 1781.8 Hz   nearest bin 1781.2 Hz (0.5 away) peak 0.9931
  //     band 67   centre 4906.3 Hz   nearest bin 4906.2 Hz (0.03 away) peak 0.9998
  //
  // torchaudio's filterbank has exactly the same property, so a gate asserting
  // peak == 1 would have been red on a CORRECT implementation. Which is worth
  // saying plainly: the obvious invariant for this defect does not exist, and
  // that is why the invariant suite could not see it and why L1 -- a comparison
  // against an independent reference -- is the only thing that does.
  //
  // What it reports is the one thing that IS true and was not true before: the
  // band's peak, and where the centre sits relative to the nearest bin.
  let n_freqs = LOGMEL_N_FFT / 2 + 1
  let fb = @tarnet.tn_mel_filterbank(n_freqs, LOGMEL_N_MELS, 16000, 20.0F, 7600.0F)
  let mut m = 0
  let mut min_peak = 1.0F
  let mut min_band = -1
  while m < LOGMEL_N_MELS {
    let mut peak = 0.0F
    let mut k = 0
    while k < n_freqs {
      let v = fb[m * n_freqs + k]
      if v > peak {
        peak = v
      }
      k = k + 1
    }
    if peak < min_peak {
      min_peak = peak
      min_band = m
    }
    m = m + 1
  }
  println("L4 lowest sampled peak " + min_peak.to_string() + " at band " + min_band.to_string() + " (a bin must land near the centre; not 1.0 by construction)")
}
