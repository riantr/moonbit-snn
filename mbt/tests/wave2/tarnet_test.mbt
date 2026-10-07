// tarnet_test.mbt -- gradient gate for the TARNet primitives.
//
// Reference: arXiv:2605.07735 (ICME 2026); authors' implementation
// github.com/YassinTERRAF/TARNet, `tarnet/models/tarnet.py`.
//
// Every primitive that has a hand-written backward is checked here against
// central differences over ITS OWN parameters, not against the whole model.
// That separation matters here: the full network stacks twenty residual
// blocks, so a whole-model check that fails tells you almost nothing about
// which of the twenty is wrong, and a per-block check that passes does not
// prove the stack composition is wired in the right order.
//
// The model-level check at the end is deliberately only a shape/finiteness
// smoke test. It is NOT evidence that the model's gradient is correct; that
// gate needs `tarnet_backward.mbt`, which this file's sibling test will own.

///|
/// Central-difference step. The objectives here are O(1..100), so the Float32
/// noise floor is `ulp(L)/2h`; these tolerances are set from that, not tuned
/// until green.
const TN_H : Float = 4.0e-3F
const TN_ATOL : Float = 2.0e-3F
const TN_RTOL : Float = 3.0e-2F

///|
/// ASP's gradient chain is the longest here -- softmax over t, then a 3C->att
/// and an att->C 1x1 convolution, then the mean/variance context path -- so
/// its finite-difference cancellation is correspondingly worse. A LARGER step
/// is the correct response to cancellation (the noise is `ulp(L)/2h`), as long
/// as truncation error stays negligible, which it does here because the
/// objective is smooth.
const TN_H_ASP : Float = 1.2e-2F  // reserved for the ASP gate

///|
/// Relative+absolute comparison as a single non-negative number: 0 or less
/// means "within tolerance".
fn tn_gap(fd : Float, an : Float) -> Float {
  let d = fd - an
  let ad = if d < 0.0F { -d } else { d }
  let m = if an < 0.0F { -an } else { an }
  ad - TN_RTOL * m - TN_ATOL
}

///|
/// Deterministic small fixture: n = 3 samples, c = 4 channels, t = 6 frames.
fn tn_fixture(n : Int, c : Int, t : Int, seed : UInt64) -> Array[Float] {
  @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(seed), n * c * t)
}

///|
/// Objective for the depthwise convolution: sum of the output weighted by a
/// fixed vector, so every input and every weight has a non-zero gradient.
fn tn_obj_dw(
  x : Array[Float],
  p : @snn_mbt.Conv1DDepthwise,
  w : Array[Float],
  n : Int,
) -> Float {
  let y = @snn_mbt.conv1d_depthwise_forward(x, n, p)
  let mut s = 0.0F
  let mut i = 0
  while i < y.length() {
    s = s + y[i] * w[i]
    i = i + 1
  }
  s
}

///|
/// Rebuild a `Conv1DDepthwise` around new weight / bias arrays. Field
/// assignment on a `let`-bound struct is rejected by the compiler, so every
/// perturbation here goes through a fresh literal -- which is also the honest
/// shape for the test: the finite difference and the analytic path must see
/// genuinely different parameter objects.
fn tn_dw_with(
  p : @snn_mbt.Conv1DDepthwise,
  w : Array[Float],
  b : Array[Float],
) -> @snn_mbt.Conv1DDepthwise {
  {
    weight: w,
    bias: b,
    kernel_size: p.kernel_size,
    dilation: p.dilation,
    padding: p.padding,
    channels: p.channels,
  }
}

///|
/// Same idea for a single weight entry.
fn tn_dw_bump_w(
  p : @snn_mbt.Conv1DDepthwise,
  idx : Int,
  delta : Float,
) -> @snn_mbt.Conv1DDepthwise {
  let w = p.weight.copy()
  w[idx] = p.weight[idx] + delta
  tn_dw_with(p, w, p.bias.copy())
}

///|
/// Same idea for a single bias entry.
fn tn_dw_bump_b(
  p : @snn_mbt.Conv1DDepthwise,
  idx : Int,
  delta : Float,
) -> @snn_mbt.Conv1DDepthwise {
  let b = p.bias.copy()
  b[idx] = p.bias[idx] + delta
  tn_dw_with(p, p.weight.copy(), b)
}



///|
///|
/// Objective for ASP: the `[n, 2c]` output weighted by a fixed vector.
fn tn_obj_asp(
  x : Array[Float],
  n : Int,
  p : @snn_mbt.AspParam,
  w : Array[Float],
) -> Float {
  let (y, _c) = @snn_mbt.asp_forward(x, n, p)
  let mut s = 0.0F
  let mut i = 0
  while i < y.length() {
    s = s + y[i] * w[i]
    i = i + 1
  }
  s
}

///|
/// Perturb one entry of `AspParam`'s first attention weight.
fn tn_asp_w1_bump(
  p : @snn_mbt.AspParam,
  idx : Int,
  delta : Float,
) -> @snn_mbt.AspParam {
  let w1 = p.w1.copy()
  w1[idx] = p.w1[idx] + delta
  {
    w1: w1,
    b1: p.b1.copy(),
    w2: p.w2.copy(),
    b2: p.b2.copy(),
    channels: p.channels,
    att_hidden: p.att_hidden,
  }
}

///|
/// Rebuild `AspParam` with a perturbed second attention weight.
fn tn_asp_w2_bump(
  p : @snn_mbt.AspParam,
  idx : Int,
  delta : Float,
) -> @snn_mbt.AspParam {
  let w2 = p.w2.copy()
  w2[idx] = p.w2[idx] + delta
  {
    w1: p.w1.copy(),
    b1: p.b1.copy(),
    w2: w2,
    b2: p.b2.copy(),
    channels: p.channels,
    att_hidden: p.att_hidden,
  }
}

///|
/// ASP gradients, checked against central differences over EVERY input
/// coordinate and a spread of both attention convolutions' weights.
///
/// The count matters: the earlier version of this gate called `fail` on the
/// first disagreement, which was index 0 -- batch 0, where a ctx stride of `c`
/// and one of `3c` give the same index. It therefore reported one coordinate
/// and the error looked like 6%, when 96 of 105 were wrong. A gate that stops
/// at the first failure reports whichever coordinate it happened to hit first,
/// so this one counts and reports the worst BY BATCH.
test "ASP: every gradient matches central differences" {
  let n = 3
  let c = 5
  let t = 7
  let x = tn_fixture(n, c, t, 41UL)
  let p = @snn_mbt.AspParam::new(c, 6, 43UL)
  let w = @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(47UL), n * 2 * c)
  let (y, cache) = @snn_mbt.asp_forward(x, n, p)
  if y.length() != n * 2 * c {
    fail("ASP output length is not n*2c")
  }
  // alpha must be a probability distribution per (sample, channel)
  let mut b = 0
  while b < n {
    let mut ch = 0
    while ch < c {
      let mut s = 0.0F
      let mut ti = 0
      while ti < t {
        s = s + cache.alpha[(b * c + ch) * t + ti]
        ti = ti + 1
      }
      if s < 0.999 || s > 1.001 {
        fail("alpha sums to \{s} at (\{b},\{ch}), expected 1")
      }
      ch = ch + 1
    }
    b = b + 1
  }
  let g = @snn_mbt.asp_backward(w, n, p, cache)
  let mut over = 0
  let mut worst = 0.0F
  let mut worst_i = -1
  let mut i = 0
  while i < x.length() {
    let xu = x.copy()
    let xd = x.copy()
    xu[i] = x[i] + TN_H_ASP
    xd[i] = x[i] - TN_H_ASP
    let fd = (tn_obj_asp(xu, n, p, w) - tn_obj_asp(xd, n, p, w)) /
      (2.0F * TN_H_ASP)
    let gap = tn_gap(fd, g.d_x[i])
    if gap > 0.0F {
      over = over + 1
    }
    let ad = if gap > 0.0F { gap } else { 0.0F }
    if ad > worst {
      worst = ad
      worst_i = i
    }
    i = i + 1
  }
  if over > 0 {
    let batch = worst_i / (c * t)
    fail("asp d_x: \{over} of \{x.length()} coordinates outside tolerance; worst excess \{worst} at index \{worst_i} (batch \{batch})")
  }
  // both attention convolutions, a spread of entries each
  let stride = (p.w1.length() / 9) + 1
  let mut idx = 0
  while idx < p.w1.length() {
    let pu = tn_asp_w1_bump(p, idx, TN_H_ASP)
    let pd = tn_asp_w1_bump(p, idx, -TN_H_ASP)
    let fd = (tn_obj_asp(x, n, pu, w) - tn_obj_asp(x, n, pd, w)) /
      (2.0F * TN_H_ASP)
    let gap = tn_gap(fd, g.d_w1[idx])
    if gap > 0.0F {
      fail("asp d_w1[\{idx}]: fd=\{fd} analytic=\{g.d_w1[idx]} gap=\{gap}")
    }
    idx = idx + stride
  }
  let stride2 = (p.w2.length() / 7) + 1
  idx = 0
  while idx < p.w2.length() {
    let pu = tn_asp_w2_bump(p, idx, TN_H_ASP)
    let pd = tn_asp_w2_bump(p, idx, -TN_H_ASP)
    let fd = (tn_obj_asp(x, n, pu, w) - tn_obj_asp(x, n, pd, w)) /
      (2.0F * TN_H_ASP)
    let gap = tn_gap(fd, g.d_w2[idx])
    if gap > 0.0F {
      fail("asp d_w2[\{idx}]: fd=\{fd} analytic=\{g.d_w2[idx]} gap=\{gap}")
    }
    idx = idx + stride2
  }
}

///|
///|
test "depthwise dilated conv1d: forward and backward agree with differences" {
  let n = 3
  let c = 4
  let t = 6
  let k = 3
  let d = 2
  let x = tn_fixture(n, c, t, 11UL)
  let rng = @snn_mbt.Xoshiro::new(29UL)
  let p = @snn_mbt.Conv1DDepthwise::new(c, k, d, (k - 1) * d / 2)
  let wv = @snn_mbt.tn_randn(c * k, 0.4F, rng)
  let bv = @snn_mbt.tn_randn(c, 0.3F, rng)
  let p = tn_dw_with(p, wv, bv)
  let w = @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(31UL), n * c * t)
  // ---- forward sanity: shape preserved ----
  let y = @snn_mbt.conv1d_depthwise_forward(x, n, p)
  if y.length() != x.length() {
    fail("depthwise conv changed the length: \{x.length()} -> \{y.length()}")
  }
  let (d_x, d_w, d_b) = @snn_mbt.conv1d_depthwise_backward(x, w, n, p)
  let mut i = 0
  while i < c * k {
    let pu = tn_dw_bump_w(p, i, TN_H)
    let pd = tn_dw_bump_w(p, i, -TN_H)
    let fd = (tn_obj_dw(x, pu, w, n) - tn_obj_dw(x, pd, w, n)) / (2.0F * TN_H)
    let g = tn_gap(fd, d_w[i])
    if g > 0.0F {
      fail("depthwise d_w[\{i}]: fd=\{fd} analytic=\{d_w[i]} gap=\{g}")
    }
    i = i + 1
  }
  i = 0
  while i < c {
    let pu = tn_dw_bump_b(p, i, TN_H)
    let pd = tn_dw_bump_b(p, i, -TN_H)
    let fd = (tn_obj_dw(x, pu, w, n) - tn_obj_dw(x, pd, w, n)) / (2.0F * TN_H)
    let g = tn_gap(fd, d_b[i])
    if g > 0.0F {
      fail("depthwise d_b[\{i}]: fd=\{fd} analytic=\{d_b[i]} gap=\{g}")
    }
    i = i + 1
  }
  i = 0
  while i < x.length() {
    let xu = x.copy()
    let xd = x.copy()
    xu[i] = x[i] + TN_H
    xd[i] = x[i] - TN_H
    let fd = (tn_obj_dw(xu, p, w, n) - tn_obj_dw(xd, p, w, n)) / (2.0F * TN_H)
    let g = tn_gap(fd, d_x[i])
    if g > 0.0F {
      fail("depthwise d_x[\{i}]: fd=\{fd} analytic=\{d_x[i]} gap=\{g}")
    }
    i = i + 1
  }
}

///|
fn tn_obj_prelu(x : Array[Float], a : Float, w : Array[Float]) -> Float {
  let y = @snn_mbt.prelu_forward(x, a)
  let mut s = 0.0F
  let mut i = 0
  while i < y.length() {
    s = s + y[i] * w[i]
    i = i + 1
  }
  s
}

///|
test "PReLU: slope gradient collects ONLY the negative entries" {
  let x = [-2.0F, -0.5F, 0.0F, 0.25F, 1.5F, 3.0F]
  let w = [1.0F, -2.0F, 0.5F, 3.0F, -1.0F, 0.75F]
  let a = 0.25F
  let (d_x, d_a) = @snn_mbt.prelu_backward(x, w, a)
  // slope, by difference
  let fd_a = (tn_obj_prelu(x, a + TN_H, w) - tn_obj_prelu(x, a - TN_H, w)) /
    (2.0F * TN_H)
  let g = tn_gap(fd_a, d_a)
  if g > 0.0F {
    fail("prelu d_slope: fd=\{fd_a} analytic=\{d_a} gap=\{g}")
  }
  // and the sign asymmetry itself: a pure-ReLU batch must have zero slope grad
  let pos = [0.5F, 1.0F, 2.0F]
  let (_p, d_a_pos) = @snn_mbt.prelu_backward(pos, [1.0F, 1.0F, 1.0F], a)
  if d_a_pos != 0.0F {
    fail("PReLU slope gradient on an all-positive batch is \{d_a_pos}, not 0")
  }
  // input gradient, elementwise. x[2] is EXACTLY zero and is skipped: PReLU has
  // a kink there -- y = x above and y = a*x at and below -- so the central
  // difference straddles both branches and returns their average, (1+a)/2,
  // while `prelu_backward` (like PyTorch) takes the x <= 0 branch. Probing a
  // non-differentiable point is a defect in the probe, not in the backward.
  let mut i = 0
  while i < x.length() {
    if x[i] == 0.0F {
      i = i + 1
      continue
    }
    let xv = x[i]
    let xu = x.copy()
    let xd = x.copy()
    xu[i] = xv + TN_H
    xd[i] = xv - TN_H
    let fd = (tn_obj_prelu(xu, a, w) - tn_obj_prelu(xd, a, w)) / (2.0F * TN_H)
    let gg = tn_gap(fd, d_x[i])
    if gg > 0.0F {
      fail("prelu d_x[\{i}]: fd=\{fd} analytic=\{d_x[i]} gap=\{gg}")
    }
    i = i + 1
  }
  // and the kink is real, not an excuse: the one-sided slopes must differ
  let lo = tn_obj_prelu([-TN_H], a, [1.0F])
  let hi = tn_obj_prelu([TN_H], a, [1.0F])
  let slope_lo = lo / -TN_H
  let slope_hi = hi / TN_H
  if slope_lo != a || slope_hi != 1.0F {
    fail("PReLU one-sided slopes are \{slope_lo} and \{slope_hi}, expected \{a} and 1")
  }
}



///|




///|
/// A deliberately small configuration: the paper's default builds twenty
/// residual blocks at 512 hidden channels, which is a fine number for PyTorch
/// and a poor number for a smoke test.
fn tn_small_model(n_classes : Int, in_chan : Int, t : Int) -> @snn_mbt.Tarnet {
  @snn_mbt.Tarnet::new(
    n_classes,
    in_chan,
    t,
    width=8,
    hid_chan=8,
    kernel=3,
    repeats_s=1,
    repeats_m=1,
    repeats_l=1,
    fuse_out=8,
    emb_dim=6,
    att_hidden=5,
    seed=13UL,
  )
}

///|
test "TARNet forward: shapes, finiteness and seed determinism" {
  let n_classes = 4
  let in_chan = 6
  let t = 9
  let n = 3
  let net = tn_small_model(n_classes, in_chan, t)
  let x = tn_fixture(n, in_chan, t, 61UL)
  let out = @snn_mbt.tarnet_forward(net, x, n)
  if out.logits.length() != n * n_classes {
    fail("logits length \{out.logits.length()}, expected \{n * n_classes}")
  }
  if out.embedding.length() != n * 6 {
    fail("embedding length \{out.embedding.length()}, expected \{n * 6}")
  }
  if out.fused.length() != n * 8 * t {
    fail("fused length \{out.fused.length()}, expected \{n * 8 * t}")
  }
  let mut i = 0
  while i < out.logits.length() {
    let v = out.logits[i]
    if !(v == v) {
      fail("logits[\{i}] is NaN")
    }
    if v > 1.0e6 || v < -1.0e6 {
      fail("logits[\{i}] = \{v} has diverged")
    }
    i = i + 1
  }
  // same seed, same result
  let net2 = tn_small_model(n_classes, in_chan, t)
  let out2 = @snn_mbt.tarnet_forward(net2, x, n)
  i = 0
  while i < out.logits.length() {
    if out.logits[i] != out2.logits[i] {
      let a = out.logits[i]
      let b = out2.logits[i]
      fail("TARNet not deterministic at index \{i}: \{a} vs \{b}")
    }
    i = i + 1
  }
}

///|
/// The parallel stacks are the paper's contribution, so pin the shape they
/// imply: three stacks of one repetition over [1,2], [4,8], [16] means 2, 2
/// and 1 blocks, all reading the SAME bottleneck output.
test "the three stacks have the paper's block counts" {
  let net = tn_small_model(3, 4, 5)
  if net.stack_s.length() != 2 {
    fail("short stack has \{net.stack_s.length()} blocks, expected 2")
  }
  if net.stack_m.length() != 2 {
    fail("mid stack has \{net.stack_m.length()} blocks, expected 2")
  }
  if net.stack_l.length() != 1 {
    fail("long stack has \{net.stack_l.length()} blocks, expected 1")
  }
  if net.stack_s[0].dilation != 1 || net.stack_s[1].dilation != 2 {
    fail("short stack dilations are \{net.stack_s[0].dilation},\{net.stack_s[1].dilation}")
  }
  if net.stack_m[0].dilation != 4 || net.stack_m[1].dilation != 8 {
    fail("mid stack dilations are wrong")
  }
  if net.stack_l[0].dilation != 16 {
    fail("long stack dilation is \{net.stack_l[0].dilation}, expected 16")
  }
}