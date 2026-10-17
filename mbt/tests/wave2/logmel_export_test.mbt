// logmel_export_test.mbt -- hand the front-end's intermediate stages to Python.
//
// WHY. The whole-model autograd gate starts at a log-Mel spectrogram. That
// makes every layer from the bottleneck onwards verifiable against PyTorch and
// leaves the FRONT-END outside it. The existing log-Mel gate does not close the
// gap: every one of its checks is an invariant or a physics probe -- the FFT
// against a direct DFT, a pure tone against the mel band that contains it, the
// window against its symmetric-vs-periodic distinction, the filterbank against
// non-negativity and contiguity. Those are good checks and they are all
// self-consistency in the sense that matters here: a composition that is
// uniformly wrong can satisfy every one of them at once. Wrong `center`
// padding, a wrong hop, or an off-by-one frame offset all pass.
//
// This hands over the four stages SEPARATELY so a disagreement says which one
// moved. The waveform itself is exported too, because a reference computed from
// a different waveform agrees about nothing.
//
// `python verify/gen_logmel_golden.py <export.log>` reads it.
//
// STAGE ORDER, which is the whole point of exporting them one at a time:
//   wav -> pre-emphasis -> power spectrum -> mel energies -> log -> CMN

///|
/// The integer hash. No transcendental, so it reproduces exactly on both sides,
/// and it is deliberately broadband -- a sinusoid would excite one mel band and
/// leave the filterbank's other 79 untested.
fn tn_lm_fixture(n : Int) -> Array[Float] {
  let w : Array[Float] = Array::make(n, 0.0F)
  let mut i = 0
  while i < n {
    w[i] = Float::from_int((i * 37 + 11) % 101 - 50) / 50.0F
    i = i + 1
  }
  w
}

const TN_LM_LEN : Int = 2400

///|
/// One `key value` line per stage. `parse_logmel` in the generator splits on the
/// key and the whitespace, and asserts every expected key arrived -- a missing
/// stage is a finding, not an empty array to be compared and quietly pass.
fn tn_lm_line(key : String, v : Array[Float]) -> String {
  let mut s = key + " "
  let mut i = 0
  while i < v.length() {
    if i > 0 {
      s = s + " "
    }
    s = s + v[i].to_string()
    i = i + 1
  }
  s + "\n"
}

test "log-Mel oracle export: every stage, separately" {
  let wav = tn_lm_fixture(TN_LM_LEN)
  let filtered = @tarnet.tn_pre_emphasis(wav, coef=0.97F)
  let power = @tarnet.tn_stft_power(filtered, 512, 400, 160)
  let fb = @tarnet.tn_mel_filterbank(257, 80, 16000, 20.0F, 7600.0F)
  let mel = @tarnet.tn_log_mel(wav)

  println("TNORACLE logmel n_samples " + TN_LM_LEN.to_string() + " n_fft 512 win 400 hop 160 n_mels 80 f_min 20 f_max 7600 pre_emphasis 0.97")
  println(tn_lm_line("TNORACLE wav", wav))
  println(tn_lm_line("TNORACLE filtered", filtered))
  println(tn_lm_line("TNORACLE power", power))
  println(tn_lm_line("TNORACLE fb", fb))
  println(tn_lm_line("TNORACLE mel", mel))
}
