// asp_oracle_test.mbt -- ASP against a PyTorch autograd oracle.
//
// WHAT THIS IS FOR.
//
// `tarnet/tarnet_asp.mbt` carries a long record of an unresolved defect in
// `asp_backward`'s `d_x`, ending in "NOT SOLVED" and the claim that "the chain
// written in `asp_backward` is not the chain `asp_forward` computes". The gate
// that was measuring it was removed rather than left red.
//
// The gate that replaced it, in `tarnet_test.mbt`, compares against central
// differences at rtol 3e-2 / atol 2e-3 with h = 1.2e-2 and is green. On the
// BatchNorm1d gate it was measured that a float32 central difference of an
// objective summed over ~170 terms carries an absolute noise floor of order
// 0.1 at a 1e-2 step -- so an atol of 2e-3 sits well below the instrument's own
// noise and the green is worth much less than it appears.
//
// So: a torch autograd reference. No step size, no tolerance to tune, exact to
// float32 roundoff, and structurally incapable of agreeing with a wrong
// implementation. `verify/asp_oracle.py` emits `asp_oracle_golden.mbt`.
//
// THE FIXTURE IS DELIBERATE. On the fixture the old investigation used, every
// attention pre-activation was negative, the ReLU mask zeroed the whole
// attention-to-input path, and the context coupling was never actually
// exercised. Here ReLU is live on 26 of 126 pre-activations, so the ctx path --
// the part the investigation could not explain -- carries real signal.

///|
///|```text
///|asp_param : the oracle fixture as an AspParam
///|```

///|
///|```text
///|asp_param
///|```

fn asp_param() -> @tarnet.AspParam {
  {
    w1: asp_w1.copy(),
    b1: asp_b1.copy(),
    att_bn_gamma: asp_bn_gamma.copy(),
    att_bn_beta: asp_bn_beta.copy(),
    w2: asp_w2.copy(),
    b2: asp_b2.copy(),
    channels: ASP_C,
    att_hidden: ASP_ATT,
  }
}

///|
///|```text
///|worst_gap : coordinates outside a mixed bound, with the worst excess
///|```

///|
///|```text
///|worst_gap
///|```

///| Mixed absolute/relative rather than pure relative, because `d_x` on this
///| fixture spans 1432x between its smallest and largest coordinate and a
///| per-coordinate relative bound on such a tensor measures proximity to zero
///| rather than accuracy.
///|
fn worst_gap(
  got : Array[Float],
  want : Array[Float],
  atol : Float,
  rtol : Float,
) -> (Int, Float) {
  let mut over = 0
  let mut worst = 0.0F
  for i in 0..<want.length() {
    let diff = (got[i] - want[i]).abs()
    let excess = diff - (atol + rtol * want[i].abs())
    if excess > 0.0F {
      over = over + 1
    }
    if excess > worst {
      worst = excess
    }
  }
  (over, worst)
}

///|
///|```text
///|max_abs_gap : largest absolute deviation from the oracle
///|```

///|
///|```text
///|max_abs_gap
///|```

fn max_abs_gap(got : Array[Float], want : Array[Float]) -> Float {
  let mut worst = 0.0F
  for i in 0..<want.length() {
    let d = (got[i] - want[i]).abs()
    if d > worst { worst = d }
  }
  worst
}

///|
///|```text
///|the two floors must agree between the port and the oracle
///|```

///|
///|```text
///|ASP floors agree with tarnet's
///|```

test "ASP oracle / the variance floors agree with tarnet's constants" {
  if @tarnet.ASP_STD_FLOOR != ASP_STD_FLOOR {
    fail(
      "ASP_STD_FLOOR: tarnet has \{@tarnet.ASP_STD_FLOOR}, the oracle golden " +
      "has \{ASP_STD_FLOOR}. Regenerate verify/asp_oracle.py.",
    )
  }
  if @tarnet.ASP_VAR_FLOOR != ASP_VAR_FLOOR {
    fail(
      "ASP_VAR_FLOOR: tarnet has \{@tarnet.ASP_VAR_FLOOR}, the oracle golden " +
      "has \{ASP_VAR_FLOOR}. Regenerate verify/asp_oracle.py.",
    )
  }
}

///|
///|```text
///|A1: asp_forward matches the autograd oracle
///|```

///|
///|```text
///|A1 asp_forward vs autograd
///|```

test "ASP oracle / asp_forward matches torch autograd" {
  let p = asp_param()
  let (y, _) = @tarnet.asp_forward(asp_x.copy(), ASP_N, p)
  if y.length() != asp_out_golden.length() {
    fail("asp_forward returned \{y.length()} values, expected \{asp_out_golden.length()}")
  }
  let (over, worst) = worst_gap(y, asp_out_golden, ASP_TOL_FWD, ASP_TOL_FWD)
  if over > 0 {
    fail(
      "A1: asp_forward diverges from the autograd oracle on \{over}/" +
      "\{asp_out_golden.length()} coordinates, worst excess \{worst}, " +
      "worst absolute \{max_abs_gap(y, asp_out_golden)}",
    )
  }
}

///|
///|```text
///|A2: asp_backward's d_x matches the autograd oracle
///|```

///|
///|```text
///|A2 d_x vs autograd
///|```

///| This is the gate the old investigation could not satisfy. It is the reason
///| ASP's BatchNorm1d is still not wired into `asp_forward`: a layer whose
///| defining property is cross-sample coupling cannot be added on top of a
///| `d_x` whose correctness is unknown.
///|
test "ASP oracle / asp_backward d_x matches torch autograd" {
  let p = asp_param()
  let (_, cache) = @tarnet.asp_forward(asp_x.copy(), ASP_N, p)
  let g = @tarnet.asp_backward(asp_g.copy(), ASP_N, p, cache)
  let (over, worst) = worst_gap(g.d_x, asp_dx_golden, ASP_ATOL_GRAD, ASP_TOL_GRAD)
  if over > 0 {
    let worst_abs = max_abs_gap(g.d_x, asp_dx_golden)
    let scale = asp_dx_golden[g.d_x.length() / 2].abs()
    fail(
      "A2: d_x diverges from the autograd oracle on \{over}/" +
      "\{asp_dx_golden.length()} coordinates. worst absolute \{worst_abs}, " +
      "worst excess \{worst}. |golden| scale here is \{scale}, so this is " +
      "\{worst_abs / scale}x the middle of the gradient's range.",
    )
  }
}

///|
///|```text
///|A3: the attention convolutions' own gradients match
///|```

///|
///|```text
///|A3 attention weight gradients vs autograd
///|```

///| Checked separately from `d_x` because they fail differently: a wrong `d_x`
///| can coexist with correct weight gradients if the error is confined to the
///| input path, and vice versa. A single combined number would hide which.
///|
test "ASP oracle / d_w1, d_b1, d_w2 match torch autograd" {
  let p = asp_param()
  let (_, cache) = @tarnet.asp_forward(asp_x.copy(), ASP_N, p)
  let g = @tarnet.asp_backward(asp_g.copy(), ASP_N, p, cache)
  let (o1, _) = worst_gap(g.d_w1, asp_dw1_golden, ASP_ATOL_GRAD, ASP_TOL_GRAD)
  let (o2, _) = worst_gap(g.d_b1, asp_db1_golden, ASP_ATOL_GRAD, ASP_TOL_GRAD)
  let (o3, _) = worst_gap(g.d_w2, asp_dw2_golden, ASP_ATOL_GRAD, ASP_TOL_GRAD)
  if o1 > 0 || o2 > 0 || o3 > 0 {
    fail(
      "A3: attention gradients diverge -- d_w1 \{o1}/\{asp_dw1_golden.length()}, " +
      "d_b1 \{o2}/\{asp_db1_golden.length()}, d_w2 \{o3}/\{asp_dw2_golden.length()}. " +
      "worst absolute d_w1 \{max_abs_gap(g.d_w1, asp_dw1_golden)}",
    )
  }
}

///|
///|```text
///|A3b: the BatchNorm1d's affine gradients match torch autograd
///|```

///|
///|```text
///|A3b BatchNorm1d affine gradients vs autograd
///|```

///| The reference's `nn.BatchNorm1d(att_hidden)` has `affine=True`, so gamma and
///| beta are trained parameters and their gradients have to come out of the
///| backward. They are checked separately from the convolution weights because
///| the most likely hand-transcription error when inserting this layer is to
///| swap the two, and a swapped pair shows up as two individually plausible
///| arrays rather than as a magnitude mismatch.
///
///| One coordinate of each is legitimately zero -- attention unit 2's ReLU is
///| fully masked on this fixture, so its BatchNorm input is identically zero and
///| nothing reaches it. The absolute term carries those coordinates, which is
///| the other reason this suite uses a mixed bound.
///|
test "ASP oracle / d_att_bn_gamma and d_att_bn_beta match torch autograd" {
  let p = asp_param()
  let (_, cache) = @tarnet.asp_forward(asp_x.copy(), ASP_N, p)
  let g = @tarnet.asp_backward(asp_g.copy(), ASP_N, p, cache)
  let (og, wg) = worst_gap(
    g.d_att_bn_gamma,
    asp_dbn_gamma_golden,
    ASP_ATOL_GRAD,
    ASP_TOL_GRAD,
  )
  if og > 0 {
    fail(
      "A3b: d_att_bn_gamma diverges on \{og}/\{ASP_ATT}, worst excess \{wg}, " +
      "worst absolute \{max_abs_gap(g.d_att_bn_gamma, asp_dbn_gamma_golden)}. " +
      "A swapped gamma/beta pair is the usual cause.",
    )
  }
  let (ob, wb) = worst_gap(
    g.d_att_bn_beta,
    asp_dbn_beta_golden,
    ASP_ATOL_GRAD,
    ASP_TOL_GRAD,
  )
  if ob > 0 {
    fail(
      "A3b: d_att_bn_beta diverges on \{ob}/\{ASP_ATT}, worst excess \{wb}, " +
      "worst absolute \{max_abs_gap(g.d_att_bn_beta, asp_dbn_beta_golden)}.",
    )
  }
}

///|
///|```text
///|A3c: the BatchNorm1d is actually in the forward path
///|```

///|
///|```text
///|A3c the BatchNorm1d is live
///|```

///| Guards against the failure this whole change could have shipped: the
///| affine wired up, the gradients flowing, and the layer itself quietly not
/// applied. `cache.att_bn_cache` records the statistics the forward used, so a
/// layer that was constructed and then skipped leaves its cache in a state the
/// real forward never produces -- `nhw = n * t` rather than something smaller.
///|
test "ASP oracle / the BatchNorm1d reduced over the batch and the time axis" {
  let p = asp_param()
  let (_, cache) = @tarnet.asp_forward(asp_x.copy(), ASP_N, p)
  let bc = cache.att_bn_cache
  if bc.h != 1 {
    fail("A3c: the attention BatchNorm cache has h = \{bc.h}, expected 1")
  }
  if bc.nhw != ASP_N * ASP_T {
    fail(
      "A3c: the attention BatchNorm reduced over \{bc.nhw} values per unit, " +
      "expected n*t = \{ASP_N * ASP_T}. That is the whole cross-sample coupling " +
      "the reference relies on.",
    )
  }
  if !bc.coupled {
    fail(
      "A3c: the attention BatchNorm ran in inference mode, so its d_input " +
      "carries no coupling terms and the gradient would be wrong in training.",
    )
  }
  // `BatchNorm2d`'s affine fields are private to `nn`, so this cannot read
  // gamma and beta off the layer; the cache's `c` says how many units the
  // normalisation actually ran over, which is the thing worth pinning.
  if bc.c != ASP_ATT {
    fail(
      "A3c: the attention BatchNorm ran over \{bc.c} channels, expected one " +
      "per attention unit (\{ASP_ATT}).",
    )
  }
  if p.att_bn_gamma.length() != ASP_ATT || p.att_bn_beta.length() != ASP_ATT {
    fail(
      "A3c: the BatchNorm affine has \{p.att_bn_gamma.length()}/" +
      "\{p.att_bn_beta.length()} entries, expected \{ASP_ATT} of each.",
    )
  }
}

///|
///|```text
///|A4: d_b2 is exactly zero, because softmax is shift-invariant
///|```

///|
///|```text
///|A4 d_b2 is exactly zero
///|```

///| Not a free assertion. Adding a constant to every logit of a channel leaves
///| its softmax unchanged, so the gradient with respect to the second
///| convolution's bias is exactly zero -- in exact arithmetic and in torch
///| alike (the oracle reports `d_b2 absmax = 0`). A non-zero `d_b2` is therefore
///| a real defect even though no relative tolerance can express it.
///
///| The bound is 1e-5 because the exact value is zero and the only thing that
///| can produce a non-zero reading is float32 roundoff carried through the
///| softmax, which measures 1.5e-6 on this fixture. That is three orders of
///| magnitude below the smallest defect the gate needs to catch: a `d_b2` that
///| is wrong by even 1% of a typical logit gradient would be ~1e-5 or larger.
///|
test "ASP oracle / d_b2 is exactly zero, as softmax shift-invariance requires" {
  let p = asp_param()
  let (_, cache) = @tarnet.asp_forward(asp_x.copy(), ASP_N, p)
  let g = @tarnet.asp_backward(asp_g.copy(), ASP_N, p, cache)
  let mut worst = 0.0F
  for i in 0..<g.d_b2.length() {
    let a = g.d_b2[i].abs()
    if a > worst { worst = a }
  }
  if worst > 1.0e-5F {
    fail(
      "A4: d_b2 should be identically zero -- a constant shift of a channel's " +
      "logits leaves its softmax unchanged. Worst |d_b2| is \{worst}.",
    )
  }
}

///|
///|```text
///|A5: the fixture really does exercise the context path
///|```

///|
///|```text
///|A5 the ReLU mask leaves signal
///|```

///| Guards the fixture rather than the code. If the attention pre-activations
///| ever all go negative, `d_conv1` becomes identically zero, the context
///| coupling is never differentiated, and A2 would pass while testing nothing
///| about the path it exists to test. That is what happened on the fixture the
///| previous investigation used.
///|
test "ASP oracle / the ReLU mask leaves the attention path live" {
  let p = asp_param()
  let (_, cache) = @tarnet.asp_forward(asp_x.copy(), ASP_N, p)
  let mut live = 0
  let mut min_abs = 1.0e30F
  for i in 0..<cache.pre.length() {
    let v = cache.pre[i]
    if v > 0.0F { live = live + 1 }
    let a = v.abs()
    if a < min_abs { min_abs = a }
  }
  if live != ASP_RELU_LIVE {
    fail(
      "A5: ReLU is live on \{live}/\{cache.pre.length()} pre-activations, the " +
      "golden says \{ASP_RELU_LIVE}. min |pre| = \{min_abs}. Either the code " +
      "or the generator moved -- compare that against the oracle's.",
    )
  }
  if live == 0 {
    fail("A5: ReLU masked the entire attention path; the context coupling is untested")
  }
  // A pre-activation close to zero is a kink: the ReLU derivative flips on a
  // difference of one float32 ulp, so the attention weight gradients become a
  // function of summation order rather than of the mathematics.
  //
  // The floor is 1e-4, and the scale is the arithmetic rather than taste. The
  // context sums 3c = 15 terms of magnitude ~2, where a float32 ulp is ~2.4e-7;
  // accumulated over the sum that is order 1e-6, which is the largest
  // disagreement two correct summation orders can produce here. The generator
  // searches for a b1 offset that maximises the distance to the kink subject to
  // both signs staying above a quarter of the activations, and lands at
  // min |pre| = 0.0047 -- some three orders of magnitude clear of the floor and
  // some four and a half clear of the noise. An offset chosen without the sign
  // constraint would maximise the margin by switching ReLU off almost entirely,
  // which tests less; the balance costs margin and is the right trade.
  if min_abs < 1.0e-4F {
    fail(
      "A5: min |pre| = \{min_abs}, below the 1e-4 floor. Two correct float32 " +
      "summation orders disagree by order 1e-6 here, so at this margin a " +
      "disagreement can flip a ReLU derivative and make d_w1 a function of " +
      "summation order rather than of the mathematics. Regenerate the " +
      "fixture with verify/asp_oracle.py.",
    )
  }
  if min_abs < ASP_RELU_MIN - 1.0e-6F {
    fail(
      "A5: min |pre| = \{min_abs} but the generator recorded " +
      "\{ASP_RELU_MIN}. The code and the golden have drifted apart.",
    )
  }
}