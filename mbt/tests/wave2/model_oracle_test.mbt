// model_oracle_test.mbt -- the whole TARNet against PyTorch autograd.
//
// WHAT THIS GATES THAT NOTHING ELSE DOES. Every other gate in the tree compares
// the port against itself: a finite difference of its own forward, or a value
// copied out of its own cache. Those cannot see a forward that is wrong in a
// self-CONSISTENT way, and the whole-model wiring is exactly where that lives.
// Concat the three scale stacks in the wrong order and the forward is wrong and
// the backward is wrong in the matching way; every finite difference agrees with
// both, the loss still descends, and the model optimises a network that is not
// the paper's.
//
// `tarnet_model_test.mbt` gets this far and stops at the same line: it proves
// the backward is the derivative of the forward. This proves the forward is the
// one PyTorch computes.
//
// The reference is `verify/gen_model_oracle.py`, which transcribes the network
// from `_build/tarnet_ref/tarnet/models/tarnet.py` rather than from this port.
// Its own selftest (`python verify/gen_model_oracle.py --selftest`) checks the
// oracle against float64 directional finite differences of its own forward, and
// MUST be run before a failure here is read as a failure of the port. It has
// six defects of its own behind it; the last one found would have made this gate
// report a confident, entirely fictional answer.
//
// Regenerating the golden needs the MoonBit export first:
//
//   moon test -p moonbit-snn/mbt/tests/wave2 *> $env:TEMP\tnexp.log
//   python verify/gen_model_oracle.py $env:TEMP\tnexp.log
//
// The fixture is byte-for-byte the one `model_export_test.mbt` hands over, and
// the one `gen_model_oracle.py` is hardcoded to. Change one, change all three.

///|
/// Reproduces the oracle's integer-hash fixture.
///
/// `((b*t + ti)*37 + ch*101 + seed) % 71 - 35`, then `base + scale*h/35`. The
/// `%` modulus and the `35` scaling are arbitrary-looking, and that is the point:
/// an irregular lattice makes it impossible for a wiring mistake to cancel out
/// against a convenient one. The frame count appears in BOTH the allocation and
/// the hash, because getting that wrong is exactly the bug that turned
/// `d_logits` into six frames and made it broadcast.
fn tn_hash_fixture(seed : Int, c : Int, t : Int) -> Array[Float] {
  let n = 3
  let x = Array::make(n * c * t, 0.0F)
  let mut b = 0
  while b < n {
    let mut ch = 0
    while ch < c {
      let base = 0.5F * Float::from_int(ch) - 2.0F
      let scale = 0.3F + 0.2F * Float::from_int(ch)
      let mut ti = 0
      while ti < t {
        let h = ((b * t + ti) * 37 + ch * 101 + seed) % 71 - 35
        x[(b * c + ch) * t + ti] = base + scale * Float::from_int(h) / 35.0F
        ti = ti + 1
      }
      ch = ch + 1
    }
    b = b + 1
  }
  x
}

fn tn_oracle_fixture() -> @tarnet.Tarnet {
  @tarnet.Tarnet::new(
    4,
    6,
    6,
    width=8,
    hid_chan=8,
    kernel=3,
    repeats_s=1,
    repeats_m=1,
    repeats_l=1,
    fuse_out=8,
    emb_dim=8,
    att_hidden=8,
    seed=141UL,
  )
}

///|
/// The bound is `atol + rtol*|golden|`, and both constants come from measuring
/// the two populations the deviation actually splits into -- not from tightening
/// until green. `tn_split_report` re-measures them on every run.
///
///     |golden| >= 1e-2   n = 1348   worst RELATIVE  9.0e-04
///     |golden| <  1e-2   n = 2225   worst ABSOLUTE  7.6e-06
///     worst absolute over all 3573:                  6.8e-05
///
/// The near-zero population needs an absolute bound because it has no scale to
/// be relative to: four parameter segments have STRUCTURALLY zero gradient (a
/// constant offset followed by a shift-invariant BatchNorm, or a Softmax along
/// the axis it is constant on), and on those both implementations are ~1e-06
/// noise. Measured on `fc_b`, the segment the gate flagged first:
///
///     port   -2.86e-06 -1.79e-07 -4.77e-07  1.79e-07 -5.96e-08 -1.43e-06  5.96e-07  1.91e-06
///     golden  1.01e-06  8.94e-08 -6.82e-07  0.00e+00  2.98e-08  2.98e-07  6.71e-08  1.51e-06
///
/// Neither is zero and neither is wrong; a purely relative bound would divide
/// 3.9e-06 by 1e-06 and report a 2.8 "excess" for a perfect match.
///
/// 2e-4 is ~3x the measured worst absolute, and the relative term is a small
/// addition on top rather than the main instrument: it dominates only for the
/// largest golden (1.5e-3 at |golden| = 15).
///
/// WHAT PROVES THIS BOUND CAN FAIL. The identical comparison, run against the
/// identical golden with the bottleneck's and the MFA's 1x1 convolution routed
/// through `linear_forward` instead of `tn_conv1x1_forward`:
///
///     quantity   worst abs   |golden| absmax
///     logits         0.302          0.279
///     embedding      2.352          1.411
///     d_input        0.207          0.232
///     d_param       14.002         14.950
///
/// 14.002 against a bound of 2e-4 + 1e-4*15 = 1.7e-3 -- it misses by a factor of
/// 8000. That measurement is the canary, and it is why this bound is safe to
/// trust: it is four orders of magnitude from the defect and one order from the
/// noise.
const TN_MODEL_ATOL : Float = 2.0e-4F
const TN_MODEL_RTOL : Float = 1.0e-4F

///|
/// Count the coordinates outside `atol + rtol*|golden|`, and the worst RELATIVE
/// excess. Returns both: a count alone cannot distinguish one systematic fault
/// from twenty scattered ones, and the two need completely different
/// investigations.
fn tn_count_over(
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
    let bound = atol + rtol * aw
    let d = got[i] - w
    let ad = if d < 0.0F { -d } else { d }
    if ad - bound > 0.0F {
      over = over + 1
      let rel = (ad - bound) / aw
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
/// The first few coordinates outside the bound, with both values. The COUNT
/// says how bad; the INDICES say where, and a tight cluster of 26 inside one
/// parameter segment is a different investigation entirely from 26 scattered
/// across the network.
fn tn_offenders(
  got : Array[Float],
  want : Array[Float],
  atol : Float,
  rtol : Float,
) -> String {
  let mut s = ""
  let mut i = 0
  while i < want.length() && s.length() < 12 {
    let w = want[i]
    let aw = if w < 0.0F { -w } else { w }
    let bound = atol + rtol * aw
    let d = got[i] - w
    let ad = if d < 0.0F { -d } else { d }
    if ad - bound > 0.0F {
      s = s + " [" + i.to_string() + " port " + got[i].to_string() + " golden " + w.to_string() + " bound " + bound.to_string() + "]"
    }
    i = i + 1
  }
  s
}

///|
/// Deviation summary, printed on every run whether or not it fails.
fn tn_report(label : String, got : Array[Float], want : Array[Float]) -> Unit {
  if got.length() != want.length() {
    println(label + ": LENGTH MISMATCH, port " + got.length().to_string() + " golden " + want.length().to_string())
    return
  }
  let (over, worst, worst_i) = tn_count_over(
    got, want, TN_MODEL_ATOL, TN_MODEL_RTOL,
  )
  let mut scale = 0.0F
  let mut i = 0
  while i < want.length() {
    let aw = if want[i] < 0.0F { -want[i] } else { want[i] }
    if aw > scale {
      scale = aw
    }
    i = i + 1
  }
  println(label + ": n " + got.length().to_string() + "  over " + over.to_string() + "  worst excess " + worst.to_string() + " at [" + worst_i.to_string() + "]  |golden| absmax " + scale.to_string())
}

///|
/// Check one quantity against the golden and return a failure message, or ""
/// when it passes. Returning the message rather than calling `fail` keeps the
/// `fail` in the test body: MoonBit only allows `fail` in a function with an
/// error type, and a helper that raised would force every caller to have one.
fn tn_gate(
  label : String,
  got : Array[Float],
  want : Array[Float],
) -> String {
  tn_report(label, got, want)
  if got.length() != want.length() {
    return label + ": port has " + got.length().to_string() + " values, the golden has " + want.length().to_string()
  }
  let (over, worst, worst_i) = tn_count_over(
    got, want, TN_MODEL_ATOL, TN_MODEL_RTOL,
  )
  if over > 0 {
    return label + ": " + over.to_string() + " of " + want.length().to_string() + " coordinates are outside atol + rtol*|golden|; worst excess " + worst.to_string() + " at [" + worst_i.to_string() + "]" + tn_offenders(got, want, TN_MODEL_ATOL, TN_MODEL_RTOL)
  }
  ""
}

test "M1: logits and embedding vs autograd" {
  let net = tn_oracle_fixture()
  let x = tn_hash_fixture(0, 6, 6)
  let d_logits = tn_hash_fixture(500, 4, 1)
  let (out, cache) = @tarnet.tarnet_forward_full(net, x, 3)
  ignore(@tarnet.tarnet_backward(net, 3, cache, d_logits, 0.0F))
  let m1 = tn_gate("M1 logits", out.logits, model_logits_golden)
  let m2 = tn_gate("M1 embedding", out.embedding, model_emb_golden)
  if m1 != "" {
    fail(m1)
  }
  if m2 != "" {
    fail(m2)
  }
}

test "M2: input and parameter gradients vs autograd" {
  let net = tn_oracle_fixture()
  let x = tn_hash_fixture(0, 6, 6)
  let d_logits = tn_hash_fixture(500, 4, 1)
  let (_out, cache) = @tarnet.tarnet_forward_full(net, x, 3)
  let (gf, dx) = @tarnet.tarnet_backward(net, 3, cache, d_logits, 0.0F)
  let m1 = tn_gate("M2 d_input", dx, model_dx_golden)
  let m2 = tn_gate("M2 d_param", gf, model_dp_golden)
  if m1 != "" {
    fail(m1)
  }
  if m2 != "" {
    fail(m2)
  }
}

///|
/// Split the deviation into two populations, because they are governed by
/// different things and one tolerance cannot honestly cover both.
///
///   * coordinates whose golden value is NOT near zero are limited by float32
///     accumulation over the twelve-stage pipeline -- a RELATIVE bound;
///   * coordinates whose golden value IS near zero have no scale to be relative
///     to. Four parameter segments have structurally zero gradient, and on those
///     the port's BatchNorm backward leaves a residue set by the magnitude of
///     the gradients AROUND it, not by the entry itself -- an ABSOLUTE bound.
///
/// Printing both maxima, and the size of each population, is what makes the two
/// constants derivable instead of guessed.
fn tn_split_report(label : String, got : Array[Float], want : Array[Float]) -> Unit {
  let big_cut = 0.01F
  let mut n_big = 0
  let mut worst_big = 0.0F
  let mut worst_big_i = -1
  let mut n_small = 0
  let mut worst_small = 0.0F
  let mut worst_small_i = -1
  let mut i = 0
  while i < want.length() {
    let w = want[i]
    let aw = if w < 0.0F { -w } else { w }
    let d = got[i] - w
    let ad = if d < 0.0F { -d } else { d }
    if aw >= big_cut {
      n_big = n_big + 1
      let r = ad / aw
      if r > worst_big {
        worst_big = r
        worst_big_i = i
      }
    } else {
      n_small = n_small + 1
      if ad > worst_small {
        worst_small = ad
        worst_small_i = i
      }
    }
    i = i + 1
  }
  println(label + ": |golden|>=1e-2  n " + n_big.to_string() + "  worst relative " + worst_big.to_string() + " at [" + worst_big_i.to_string() + "]")
  println(label + ": |golden|< 1e-2  n " + n_small.to_string() + "  worst absolute " + worst_small.to_string() + " at [" + worst_small_i.to_string() + "]")
}

test "M3: re-measure the deviation split the two tolerances come from" {
  // Always green, and always printed. It is the evidence behind TN_MODEL_ATOL
  // and TN_MODEL_RTOL, and it is here so that a future change in the port's
  // precision shows up as a MOVING NUMBER rather than as a gate that quietly
  // stops testing anything.
  let net = tn_oracle_fixture()
  let x = tn_hash_fixture(0, 6, 6)
  let d_logits = tn_hash_fixture(500, 4, 1)
  let (_out, cache) = @tarnet.tarnet_forward_full(net, x, 3)
  let (gf, dx) = @tarnet.tarnet_backward(net, 3, cache, d_logits, 0.0F)
  tn_split_report("split d_param", gf, model_dp_golden)
  tn_split_report("split d_input", dx, model_dx_golden)
}

test "M0: the fixtures really are [n, classes, 1] and [n, in_chan, t]" {
  // ONE frame, not six: the ASP head pools over time, so the heads see a single
  // frame per sample. This is the array whose width the oracle got wrong, and
  // getting it wrong did not fail -- it broadcast.
  let d_logits = tn_hash_fixture(500, 4, 1)
  if d_logits.length() != 3 * 4 * 1 {
    fail("d_logits has " + d_logits.length().to_string() + " elements, expected 12")
  }
  let x = tn_hash_fixture(0, 6, 6)
  if x.length() != 3 * 6 * 6 {
    fail("x has " + x.length().to_string() + " elements, expected 108")
  }
}
