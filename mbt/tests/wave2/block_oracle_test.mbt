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

fn goldens_for(i : Int) -> (Array[Float], Array[Float]) {
  if i == 0 {
    (blk0_out_golden, blk0_dx_golden)
  } else if i == 1 {
    (blk1_out_golden, blk1_dx_golden)
  } else if i == 2 {
    (blk2_out_golden, blk2_dx_golden)
  } else if i == 3 {
    (blk3_out_golden, blk3_dx_golden)
  } else {
    (blk4_out_golden, blk4_dx_golden)
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
    let (gout, _gdx) = goldens_for(i)
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
    let (_gout, gdx) = goldens_for(i)
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
