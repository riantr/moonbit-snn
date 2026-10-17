// block_oracle_test.mbt -- Conv1DBlock against a PyTorch autograd oracle.
//
// WHAT THIS COVERS, and why it is not the same as the finite-difference suite.
//
// The block is the model's most error-prone composition: a 1x1 convolution,
// PReLU, a global layer norm, a depthwise DILATED convolution with zero
// padding, PReLU again, a second global layer norm, a 1x1 convolution, and the
// paper's CTSE gate, added residually. One instance of this code covers every
// block in the network -- five in the fixture below, twelve at the paper's
// default width -- so one transcription of the oracle covers all of them.
//
// The point of an autograd reference is that it cannot agree with a wrong
// implementation by construction. A central difference of the block's own
// forward would: it reports the gradient of whatever the forward computes, so a
// forward that is wrong in a self-consistent way passes at any tolerance loose
// enough for float32. That is not hypothetical in this package -- `asp_forward`
// wrote its context buffer at the wrong stride, and `batch_norm2d_forward`
// cancelled catastrophically in its variance, both with the finite-difference
// suite green.
//
// The oracle is transcribed from the reference
// (`_build/tarnet_ref/tarnet/models/tarnet.py`, classes Conv1DBlock and
// ChannelTimeSenseSELayer) rather than from this port, so an error in the
// MoonBit block surfaces as a disagreement instead of being reproduced on both
// sides.
//
// WHAT IT DOES NOT COVER. The parameter gradients -- d_pw1, d_ln1_gamma, d_dw,
// d_prelu, d_ctse and the rest -- are still checked only by finite differences.
// `d_x` traverses every one of those layers, so a layer whose INPUT gradient is
// wrong is caught, but a weight-gradient formula that is wrong while `d_x`
// happens to be right would not be. That gap is why the goldens carry only
// `out` and `dx` for now and this comment says so, rather than the suite
// implying a coverage it does not have.

///|
///|```text
///|blk_fixture : deterministic [n, c, t] fixture, matched by the Python oracle
///|```

///|
///|```text
///|blk_fixture
///|```

///| Integer-hash based, so the MoonBit side and `verify/gen_block_oracle.py`
///| build the same numbers with integer arithmetic and no shared RNG. The
///| per-channel mean and scale are distinct, so a channel-mixing bug cannot
///| hide behind a symmetric configuration.
///|
fn blk_fixture(n : Int, c : Int, t : Int, seed : Int) -> Array[Float] {
  let out : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base_v = 0.5F * Float::from_int(ch) - 2.0F
      let scale = 0.3F + 0.2F * Float::from_int(ch)
      for ti in 0..<t {
        let h = ((b * t + ti) * 37 + ch * 101 + seed) % 71 - 35
        out[(b * c + ch) * t + ti] = base_v + scale * Float::from_int(h) / 35.0F
      }
    }
  }
  out
}

///|
///|```text
///|the i-th block of the fixture, in tarnet_flatten's order
///|```

///|
///|```text
///|block_i
///|```

///| `tarnet_flatten` walks stack_s, stack_m, stack_l, and the fixture builds
///| each with repeats of 1, so the order is s[0], s[1], m[0], m[1], l[0].
///|
///|
///|```text
///|BLOCK_ATOL / BLOCK_RTOL : the block gate's mixed bound
///|```

///|
///|```text
///|BLOCK_ATOL
///|```

///| DERIVED, not fitted. Measured against this oracle across all five
///| dilations, the worst disagreement is 1.3e-04 absolute on quantities whose
///| scale is 2.7 to 3.2 -- about 4.5e-05 relative -- and the forward agrees to
///| 6.2e-05 absolute. Both are float32 rounding on a chain that sums a depthwise
///| convolution, two global layer norms and a three-path gate; neither is
///| anywhere near a defect.
///
///| The bound is roughly thirty times that floor. It is still far below anything
///| worth catching: the two defects this oracle exists to catch were a stride
///| error worth 17% and a variance error worth 2.09%, both three orders of
///| magnitude above the margin here.
///|
const BLOCK_ATOL : Float = 1.0e-3F

///|
///|```text
///|BLOCK_RTOL
///|```

const BLOCK_RTOL : Float = 1.0e-3F

fn block_i(net : @tarnet.Tarnet, i : Int) -> @tarnet.TarnetBlock {
  if i == 0 { net.stack_s[0] }
  else if i == 1 { net.stack_s[1] }
  else if i == 2 { net.stack_m[0] }
  else if i == 3 { net.stack_m[1] }
  else { net.stack_l[0] }
}

///|
///|```text
///|goldens_for : the oracle arrays for block i
///|```

///|
///|```text
///|goldens_for
///|```

fn goldens_for(i : Int) -> (Array[Float], Array[Float], Array[Float]) {
  if i == 0 {
    (blk0_out_golden, blk0_dx_golden, blk0_dp_golden)
  } else if i == 1 {
    (blk1_out_golden, blk1_dx_golden, blk1_dp_golden)
  } else if i == 2 {
    (blk2_out_golden, blk2_dx_golden, blk2_dp_golden)
  } else if i == 3 {
    (blk3_out_golden, blk3_dx_golden, blk3_dp_golden)
  } else {
    (blk4_out_golden, blk4_dx_golden, blk4_dp_golden)
  }
}

///|
///|```text
///|worst : coordinates outside a mixed bound, and the worst relative excess
///|```

///|
///|```text
///|worst
///|```

///| Mixed absolute/relative rather than pure relative, for the reason the
///| BatchNorm1d gate gives: these quantities are built from cancelling
///| differences, so their precision is set by their intermediates rather than
///| by their own size, and a pure relative bound on such a tensor measures how
///| near zero each coordinate happens to sit.
///|
fn worst(
  got : Array[Float],
  want : Array[Float],
  atol : Float,
  rtol : Float,
) -> (Int, Float) {
  let mut over = 0
  let mut worst_abs = 0.0F
  let mut scale = 0.0F
  for i in 0..<want.length() {
    let diff = (got[i] - want[i]).abs()
    if diff > atol + rtol * want[i].abs() { over = over + 1 }
    if diff > worst_abs { worst_abs = diff }
    let a = want[i].abs()
    if a > scale { scale = a }
  }
  // Reported relative to the golden's own scale, because an excess measured
  // against a bound this small is a number without a scale attached.
  (over, if scale > 0.0F { worst_abs / scale } else { worst_abs })
}

///|
///|```text
///|B1: the block forward matches the autograd oracle at every dilation
///|```

///|
///|```text
///|B1 block forward vs autograd
///|```

test "Block oracle / forward matches torch autograd at all five dilations" {
  let (net, _x, _targets) = tn_model_fixture(
    BLOCK_N, 4, 6, BLOCK_T, 141UL,
  )
  let x = blk_fixture(BLOCK_N, BLOCK_C, BLOCK_T, 0)
  let mut total_over = 0
  let mut worst_rel = 0.0F
  let mut worst_at = -1
  for i in 0..<block_dilations.length() {
    let b = block_i(net, i)
    if b.dilation != block_dilations[i] {
      fail(
        "B1: block \{i} has dilation \{b.dilation}, the golden says " +
        "\{block_dilations[i]}. The fixture and the goldens have drifted.",
      )
    }
    let (y, _cache) = @tarnet.tarnet_block_forward_full(b, x.copy(), BLOCK_N)
    let (gout, _gdx, _gdp) = goldens_for(i)
    let (over, rel) = worst(y, gout, BLOCK_ATOL, BLOCK_RTOL)
    total_over = total_over + over
    if rel > worst_rel {
      worst_rel = rel
      worst_at = i
    }
  }
  if total_over > 0 {
    fail(
      "B1: forward diverges from the autograd oracle on " +
      "\{total_over} coordinates across 5 blocks; worst deviation " +
      "\{worst_rel} at block \{worst_at} (dilation " +
      "\{block_dilations[worst_at]}).",
    )
  }
}

///|
///|```text
///|B2: d_x matches the autograd oracle at every dilation
///|```

///|
///|```text
///|B2 d_x vs autograd
///|```

///| `d_x` traverses the whole block backward -- CTSE, both layer norms, the
///| depthwise dilated convolution, both 1x1 convolutions and both PReLUs -- so
///| a layer whose input gradient is wrong shows up here even though the
///| parameter gradients are still only finite-difference checked.
///|
test "Block oracle / d_x matches torch autograd at all five dilations" {
  let (net, _x, _targets) = tn_model_fixture(
    BLOCK_N, 4, 6, BLOCK_T, 141UL,
  )
  let x = blk_fixture(BLOCK_N, BLOCK_C, BLOCK_T, 0)
  let d_out = blk_fixture(BLOCK_N, BLOCK_C, BLOCK_T, 500)
  let mut total_over = 0
  let mut worst_rel = 0.0F
  let mut worst_at = -1
  for i in 0..<block_dilations.length() {
    let b = block_i(net, i)
    let (_y, cache) = @tarnet.tarnet_block_forward_full(b, x.copy(), BLOCK_N)
    let (dx, _g) = @tarnet.tarnet_block_backward(
      b, BLOCK_N, cache, d_out.copy(), 0.0F,
    )
    let (_gout, gdx, _gdp) = goldens_for(i)
    let (over, rel) = worst(dx, gdx, BLOCK_ATOL, BLOCK_RTOL)
    total_over = total_over + over
    if rel > worst_rel {
      worst_rel = rel
      worst_at = i
    }
  }
  if total_over > 0 {
    fail(
      "B2: d_x diverges on \{total_over} coordinates " +
      "across 5 blocks; worst deviation (relative to the golden scale) " +
      "\{worst_at} (dilation \{block_dilations[worst_at]}).",
    )
  }
}

///|
///|```text
///|segment_name : which parameter a flat gradient index belongs to
///|```

///|
///|```text
///|segment_name
///|```

///| Names the segment so a divergence can be read as "one formula is wrong"
///| rather than "some coordinates are ill-conditioned". A wrong weight
///| gradient breaks a whole contiguous array; float32 cancellation in a
///| reduction breaks scattered coordinates. The two look identical in a count
///| and nothing alike in a histogram, so the failing offsets are reported by
///| name.
///|
fn segment_name(i : Int, c : Int, hid : Int, kernel : Int, c_red : Int) -> String {
  let mut k = i
  // pw1 weight, pw1 bias, ln1 gamma, ln1 beta, dw weight, dw bias, prelu,
  // ln2 gamma, ln2 beta, pw2 weight, pw2 bias -- each advanced in turn.
  if k < c * hid { return "pw1.weight" }
  k = k - c * hid
  if k < hid { return "pw1.bias" }
  k = k - hid
  if k < hid { return "ln1.gamma" }
  k = k - hid
  if k < hid { return "ln1.beta" }
  k = k - hid
  if k < hid * kernel { return "dw.weight" }
  k = k - hid * kernel
  if k < hid { return "dw.bias" }
  k = k - hid
  if k < 1 { return "prelu.slope" }
  k = k - 1
  if k < hid { return "ln2.gamma" }
  k = k - hid
  if k < hid { return "ln2.beta" }
  k = k - hid
  if k < hid * c { return "pw2.weight" }
  k = k - hid * c
  if k < c { return "pw2.bias" }
  k = k - c
  if k < c * 3 { return "ctse.small.weight" }
  k = k - c * 3
  if k < c { return "ctse.small.bias" }
  k = k - c
  if k < c * 5 { return "ctse.mid.weight" }
  k = k - c * 5
  if k < c { return "ctse.mid.bias" }
  k = k - c
  if k < c * 9 { return "ctse.large.weight" }
  k = k - c * 9
  if k < c { return "ctse.large.bias" }
  k = k - c
  if k < 3 { return "ctse.feature_fc.weight" }
  k = k - 3
  if k < 1 { return "ctse.feature_fc.bias" }
  k = k - 1
  if k < c_red * c { return "ctse.fc1.weight" }
  k = k - c_red * c
  if k < c_red { return "ctse.fc1.bias" }
  k = k - c_red
  if k < c * c_red { return "ctse.fc2.weight" }
  k = k - c * c_red
  if k < c { return "ctse.fc2.bias" }
  "beyond"
}

///|
///|```text
///|BLOCK_ATOL_P : absolute tolerance for PARAMETER gradients
///|```

///|
///|```text
///|BLOCK_ATOL_P
///|```

///| DERIVED, and from a measurement rather than from a run that failed. The
///| first attempt at this gate used the forward's bound of 1e-3 and reported 42
///| of 449 coordinates outside it, scattered across `pw1.weight`, `pw1.bias`
///| and `dw.weight` rather than clustered in one array -- the signature of
///| conditioning, not of a wrong formula, since a wrong formula breaks a
///| contiguous array.
///|
///| So the noise floor was measured instead of guessed.
///| `verify/param_grad_64.py` recomputes this same oracle in float64 and
///| compares it to its own float32 result, with MoonBit out of the picture:
///
///|     block  dilation   scale      f32-vs-f64 max|d|   relative
///|        0      1       33.59        3.37e-02          1.00e-03
///|        1      2       29.67        9.12e-02          3.07e-03
///|        2      4       48.12        9.34e-03          1.94e-04
///|        3      8       51.90        1.22e-01          2.34e-03
///|        4     16       57.64        3.09e-02          5.36e-04
///|
///| Torch's own float32 gradient therefore disagrees with its own float64 one
///| by up to 0.122 absolute -- LARGER than the disagreement this gate was
///| measuring against MoonBit. The float32 result is the noisy side, and no
///| bound below that floor can distinguish "wrong" from "float32".
///|
///| This bound is roughly twice that floor, which is about eighteen times the
///| disagreement actually observed against MoonBit. It is a far weaker check
///| than B1 and B2, and it says so: the parameter gradients of this block span
///| six orders of magnitude (33.59 down to 5.6e-07), so the arithmetic noise is
///| comparable to the typical gradient, whose median magnitude is 0.56. A
///| parameter-gradient gate at float32 cannot do better than roughly plus or
///| minus twenty percent on the well-populated coordinates and nothing at all
///| on the tiny ones. The value of B4 is catching a wrong FORMULA -- which
///| moves whole arrays by whole factors -- not catching a few percent.
///|
const BLOCK_ATOL_P : Float = 2.5e-1F

///|
///|```text
///|BLOCK_RTOL_P : relative term for parameter gradients, deliberately zero
///|```

///|
///|```text
///|BLOCK_RTOL_P
///|```

///| Zero on purpose. A relative term on a quantity spanning six orders of
///| magnitude buys precision on coordinates that are already noise and
///| removes it from the large ones where a real defect would show.
///|
const BLOCK_RTOL_P : Float = 0.0F

///|
///|```text
///|B4 parameter gradients vs autograd
///|```

///| The 449 gradients of one block, compared positionally against
///| `tarnet_block_grad_flatten`'s order -- pw1 weight and bias, ln1 gamma and
///| beta, the depthwise weight and bias, the shared prelu slope, ln2 gamma and
///| beta, pw2 weight and bias, then the CTSE's thirteen arrays.
///
///| This is the layer `d_x` cannot check. `d_x` traverses every layer, so a
///| wrong INPUT gradient is caught by B2; a wrong WEIGHT gradient is not,
///| because the weight does not appear in the input gradient at all. A block
///| whose `d_ctse` summed over the wrong axis, or whose `d_ln1_gamma` omitted
///| the residual term, would pass B1, B2 and B3 and still train wrongly.
///
///| Positional comparison needs the two flat vectors to be the same shape, so
///| the length is asserted rather than assumed -- a gradient vector of the
///| right kind but the wrong length would otherwise be compared element by
///| element against the wrong oracle coordinate.
///|
test "Block oracle / all 449 parameter gradients match torch autograd" {
  let (net, _x, _targets) = tn_model_fixture(
    BLOCK_N, 4, 6, BLOCK_T, 141UL,
  )
  let x = blk_fixture(BLOCK_N, BLOCK_C, BLOCK_T, 0)
  let d_out = blk_fixture(BLOCK_N, BLOCK_C, BLOCK_T, 500)
  let mut total_over = 0
  let mut worst_rel = 0.0F
  let mut worst_at = -1
  for i in 0..<block_dilations.length() {
    let b = block_i(net, i)
    let (_y, cache) = @tarnet.tarnet_block_forward_full(b, x.copy(), BLOCK_N)
    let (_dx, g) = @tarnet.tarnet_block_backward(
      b, BLOCK_N, cache, d_out.copy(), 0.0F,
    )
    let flat = @tarnet.tarnet_block_grad_flatten(g, b)
    let (_o, _d, dgp) = goldens_for(i)
    if flat.length() != dgp.length() {
      fail(
        "B4: block \{i} produced \{flat.length()} parameter gradients, the " +
        "golden has \{dgp.length()}. The two flat layouts have drifted apart, " +
        "and a positional comparison would silently compare the wrong " +
        "coordinates.",
      )
    }
    let (over, rel) = worst(flat, dgp, BLOCK_ATOL_P, BLOCK_RTOL_P)
    total_over = total_over + over
    if over > 0 {
      let mut where_ = ""
      let mut shown = 0
      for j in 0..<flat.length() {
        if (flat[j] - dgp[j]).abs() > BLOCK_ATOL_P + BLOCK_RTOL_P * dgp[j].abs() {
          where_ = where_ + "\n    " + segment_name(
            j, b.se.channels, b.hid, b.dw.kernel_size, b.se.c_red,
          ) + " at index " + j.to_string()
          shown = shown + 1
          if shown >= 6 { break }
        }
      }
      fail(
        "B4: block \{i} (dilation \{block_dilations[i]}) has \{over} parameter " +
        "gradients outside tolerance; worst deviation \{rel}. First few:\{where_}",
      )
    }
    if rel > worst_rel {
      worst_rel = rel
      worst_at = i
    }
  }
  if total_over > 0 {
    fail(
      "B4: parameter gradients diverge on \{total_over} coordinates across 5 " +
      "blocks; worst deviation (relative to the golden scale) \{worst_rel} " +
      "at block \{worst_at}.",
    )
  }
}

///|
///|```text
///|B3: the fixture is not degenerate
///|```

///|
///|```text
///|B3 fixture is well conditioned
///|```

///| Guards the two gates above rather than the code. If the fixture collapsed
///| -- every block output the same, or every gradient zero -- both would pass
///| while checking nothing, which is the failure mode of a generated golden.
///|
test "Block oracle / the fixture exercises the block rather than a flat line" {
  let (net, _x, _targets) = tn_model_fixture(
    BLOCK_N, 4, 6, BLOCK_T, 141UL,
  )
  let x = blk_fixture(BLOCK_N, BLOCK_C, BLOCK_T, 0)
  let d_out = blk_fixture(BLOCK_N, BLOCK_C, BLOCK_T, 500)
  let b = block_i(net, 0)
  let (y, cache) = @tarnet.tarnet_block_forward_full(b, x.copy(), BLOCK_N)
  let (dx, _g) = @tarnet.tarnet_block_backward(b, BLOCK_N, cache, d_out.copy(), 0.0F)
  let mut y_lo = 1.0e30F
  let mut y_hi = -1.0e30F
  let mut dx_lo = 1.0e30F
  let mut dx_hi = -1.0e30F
  for i in 0..<y.length() {
    if y[i] < y_lo { y_lo = y[i] }
    if y[i] > y_hi { y_hi = y[i] }
    let a = dx[i].abs()
    if a < dx_lo { dx_lo = a }
    if a > dx_hi { dx_hi = a }
  }
  if y_hi - y_lo < 0.1F {
    fail("B3: the block output spans only \{y_hi - y_lo}; the fixture is flat")
  }
  if dx_hi < 1.0e-4F {
    fail("B3: every d_x is below 1e-4; the gradient path is not exercised")
  }
  // A dead d_x coordinate would make a per-coordinate comparison vacuous.
  if dx_lo == 0.0F {
    fail("B3: at least one d_x coordinate is exactly zero; the gradient is degenerate")
  }
}
