// se_oracle_test.mbt -- the post-MFA squeeze-excitation against autograd.
//
// WHAT THIS IS, precisely. TARNet carries TWO different squeeze-excitations and
// they are easy to confuse. The blocks carry CTSE, a three-path channel-time
// gate with kernels 3/5/9 and a shared `Linear(3,1)` across scales; that is
// covered by `block_oracle_test.mbt`, because one instance of the block code is
// every block in the network. This file covers the OTHER one: `mfa_se`, the
// plain AdaptiveAvgPool -> 1x1 -> ReLU -> 1x1 -> Sigmoid gate that the MFA
// applies to its fused features.
//
// It is used once, so its weight in the model is smaller than CTSE's. It was,
// however, checked only against central differences of its own forward -- and
// that comparison is blind to a forward which is wrong in a self-consistent
// way. The two mistakes this layer is most likely to carry are exactly the kind
// that comparison cannot see: pooling over time rather than over frames, and
// applying the gate to the pooled descriptor instead of broadcasting it back
// over every frame. Both leave the forward internally consistent.
//
// The oracle is transcribed from the reference's `SEModule`
// (`_build/tarnet_ref/tarnet/models/tarnet.py`), not from this port, and takes
// its 552 parameters from `tarnet_flatten` at a known offset, so it cannot
// silently depend on the two RNGs agreeing.

///|
///|```text
///|se_fixture : deterministic [n, c, t] input, matched by the Python oracle
///|```

///|
///|```text
///|se_fixture
///|```

///| The same integer-hash construction the block gate uses, so a regression
///| shows up as a changed shape rather than as a puzzling number.
///|
fn se_fixture(n : Int, c : Int, t : Int, seed : Int) -> Array[Float] {
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
///|SE_ATOL / SE_RTOL : bound for the forward and d_x
///|```

///|
///|```text
///|SE_ATOL
///|```

///| Derived the same way as the block gate's: the forward and `d_x` of this
///| layer are well conditioned -- the oracle reports a maximum magnitude of
///| 1.56 for the output and 1.42 for `d_x`, against a median coordinate of
///| order 1 -- so float32 rounding on a mean over six frames and two 1x1
///| convolutions lands around 1e-06, and a bound of 1e-3 leaves three orders
///| of headroom while staying far below a defect worth catching. The two
///| mistakes this gate exists for, pooling over frames instead of time and
///| failing to broadcast the gate back, are whole-signal errors.
///|
const SE_ATOL : Float = 1.0e-3F

///|
///|```text
///|SE_RTOL
///|```

const SE_RTOL : Float = 1.0e-3F

///|
///|```text
///|SE_ATOL_P / SE_RTOL_P : bound for the parameter gradients
///|```

///|
///|```text
///|SE_ATOL_P
///|```

///| Absolute, and generous relative to the forward's, for the reason
///| `verify/param_grad_64.py` established on the block: a parameter gradient is
///| a reduction, and torch's own float32 result disagrees with its float64 one
///| by up to 0.12 absolute on quantities of scale ~30. A bound tighter than
///| the arithmetic would measure float32, not correctness.
///|
const SE_ATOL_P : Float = 2.5e-1F

///|
///|```text
///|SE_RTOL_P : deliberately zero -- see the note on SE_ATOL_P
///|```

///|
///|```text
///|SE_RTOL_P
///|```

const SE_RTOL_P : Float = 0.0F

///|
///|```text
///|slice_of : a copy of len values out of flat starting at off
///|```

///|
///|```text
///|slice_of
///|```

///| A local copy because `tarnet`'s own `tn_take` is private to that package,
///| and the comparison needs slices of the flat golden rather than of a
///| parameter block.
///|
fn slice_of(flat : Array[Float], off : Int, len : Int) -> Array[Float] {
  let out : Array[Float] = Array::make(len, 0.0F)
  for i in 0..<len { out[i] = flat[off + i] }
  out
}

///|
///|```text
///|count_over : coordinates outside a mixed bound, and the worst deviation
///|```

///|
///|```text
///|count_over
///|```

fn count_over(
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
  (over, if scale > 0.0F { worst_abs / scale } else { worst_abs })
}

///|
///|```text
///|S1: the SE forward matches the autograd oracle
///|```

///|
///|```text
///|S1 SE forward vs autograd
///|```

test "SE oracle / forward matches torch autograd" {
  let (net, _x, _targets) = tn_model_fixture(
    SE_N, 4, 6, SE_T, 141UL,
  )
  let p = net.mfa_se
  if p.channels != SE_C || p.bottleneck != SE_BOTTLENECK {
    fail(
      "S1: the fixture's SE is \{p.channels}->\{p.bottleneck}, the golden is " +
      "\{SE_C}->\{SE_BOTTLENECK}. The fixture and the goldens have drifted.",
    )
  }
  let x = se_fixture(SE_N, SE_C, SE_T, 0)
  let (y, _cache) = @tarnet.se_forward_full(x, SE_N, p)
  let (over, rel) = count_over(y, se_out_golden, SE_ATOL, SE_RTOL)
  if over > 0 {
    fail(
      "S1: the SE forward diverges from the autograd oracle on \{over}/" +
      "\{se_out_golden.length()} coordinates; worst deviation \{rel}.",
    )
  }
}

///|
///|```text
///|S2: d_x matches the autograd oracle
///|```

///|
///|```text
///|S2 SE d_x vs autograd
///|```

///| `d_x` is where a broadcasting mistake shows up. If the gate were applied to
///| the pooled descriptor rather than broadcast back over every frame, the
///| forward would still normalise sensibly and `d_x` would be wrong only where
///| the frames differ.
///|
test "SE oracle / d_x matches torch autograd" {
  let (net, _x, _targets) = tn_model_fixture(
    SE_N, 4, 6, SE_T, 141UL,
  )
  let p = net.mfa_se
  let x = se_fixture(SE_N, SE_C, SE_T, 0)
  let d_out = se_fixture(SE_N, SE_C, SE_T, 500)
  let (_y, cache) = @tarnet.se_forward_full(x, SE_N, p)
  let g = @tarnet.se_backward(x, d_out, SE_N, p, cache)
  let (over, rel) = count_over(g.d_x, se_dx_golden, SE_ATOL, SE_RTOL)
  if over > 0 {
    fail(
      "S2: the SE d_x diverges from the autograd oracle on \{over}/" +
      "\{se_dx_golden.length()} coordinates; worst deviation \{rel}.",
    )
  }
}

///|
///|```text
///|S3: every parameter gradient matches the autograd oracle
///|```

///|
///|```text
///|S3 SE parameter gradients vs autograd
///|```

///| Compared segment by segment in `SeParam` order -- w1, b1, w2, b2 -- against
///| slices of the single flat golden. Segment-wise rather than flat so a
///| swapped `b1`/`b2` or a transposed `w2` is reported as the array it is
///| instead of showing up as 552 scattered coordinates.
///|
test "SE oracle / all 552 parameter gradients match torch autograd" {
  let (net, _x, _targets) = tn_model_fixture(
    SE_N, 4, 6, SE_T, 141UL,
  )
  let p = net.mfa_se
  let x = se_fixture(SE_N, SE_C, SE_T, 0)
  let d_out = se_fixture(SE_N, SE_C, SE_T, 500)
  let (_y, cache) = @tarnet.se_forward_full(x, SE_N, p)
  let g = @tarnet.se_backward(x, d_out, SE_N, p, cache)
  let bnk = SE_BOTTLENECK
  let c = SE_C
  let segs = [
    ("w1", g.w1, 0),
    ("b1", g.b1, bnk * c),
    ("w2", g.w2, bnk * c + bnk),
    ("b2", g.b2, bnk * c + bnk + c * bnk),
  ]
  for s in segs {
    let (name, arr, off) = s
    let want = slice_of(se_dp_golden, off, arr.length())
    let (over, rel) = count_over(arr, want, SE_ATOL_P, SE_RTOL_P)
    if over > 0 {
      fail(
        "S3: SE \{name} diverges on \{over}/\{arr.length()} coordinates; " +
        "worst deviation \{rel}. A transposed weight or a swapped bias looks " +
        "exactly like this.",
      )
    }
  }
}

///|
///|```text
///|S4: the fixture is not degenerate
///|```

///|
///|```text
///|S4 fixture is well conditioned
///|```

///| Guards the gates above. A fixture whose gate saturates would make the
///| layer insensitive to its weights and every comparison would pass while
///| checking nothing -- and saturation is a genuine risk here, because the gate
///| is a sigmoid of two randn-weighted sums and either could push it to an end.
///|
///| The first version of this check asked for the gate values to SPREAD, which
///| is the wrong question. It failed on a fixture whose gates all sat near 0.50,
///| which is where a sigmoid has its maximum slope and therefore the LEAST
///| saturated values it can have. Spread and saturation are not opposites.
///|
///| What matters is distance from the ends, so that is what is checked.
///|
test "SE oracle / the fixture does not saturate the gate or flatten the input" {
  let (net, _x, _targets) = tn_model_fixture(
    SE_N, 4, 6, SE_T, 141UL,
  )
  let p = net.mfa_se
  let x = se_fixture(SE_N, SE_C, SE_T, 0)
  let (y, cache) = @tarnet.se_forward_full(x, SE_N, p)
  let mut g_lo = 2.0F
  let mut g_hi = -1.0F
  for i in 0..<cache.gate.length() {
    let v = cache.gate[i]
    if v < g_lo { g_lo = v }
    if v > g_hi { g_hi = v }
  }
  if g_lo < 0.05F || g_hi > 0.95F {
    fail(
      "S4: the gate values run \{g_lo}..\{g_hi}, reaching into a saturated " +
      "region where the sigmoid's derivative vanishes and the layer stops " +
      "responding to its weights. Use an input or a weight scale that keeps " +
      "the gate off the ends.",
    )
  }
  // The pooled descriptor must differ across samples, or the gate is one number
  // broadcast and the layer is untested.
  let mut u_spread = 0.0F
  for ch in 0..<SE_C {
    let lo = cache.u[ch]
    let hi = cache.u[ch]
    for b in 1..<SE_N {
      let v = cache.u[b * SE_C + ch]
      if v < lo || v > hi { u_spread = u_spread + 1.0F }
    }
  }
  if u_spread == 0.0F {
    fail(
      "S4: the pooled descriptor is identical across all samples, so the gate " +
      "is a single number and the sample axis of this layer is untested.",
    )
  }
  // The output must actually differ across time, otherwise the broadcast-back
  // -- the thing this layer exists to do -- is not exercised.
  let mut t_spread = 0.0F
  for b in 0..<SE_N {
    for ch in 0..<SE_C {
      let a0 = y[(b * SE_C + ch) * SE_T]
      for ti in 1..<SE_T {
        let v = y[(b * SE_C + ch) * SE_T + ti]
        if (v - a0).abs() > 1.0e-4F { t_spread = t_spread + 1.0F }
      }
    }
  }
  if t_spread == 0.0F {
    fail("S4: the SE output is constant over time; the gate was not broadcast")
  }
}
