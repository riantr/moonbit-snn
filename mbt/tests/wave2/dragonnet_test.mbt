// dragonnet_test.mbt -- gradient gate for the DragonNet port.
//
// The architecture is not the risky part; the loss algebra is. Four things
// can be silently wrong and still compile, still run, and still "train":
//
//   1. dL/depsilon is zero, or has the wrong sign. epsilon only enters through
//      the targeted-regularisation term, so a chain that forgets it produces a
//      model that trains fine and never targets.
//   2. The propensity enters the two loss terms through DIFFERENT smoothings
//      (0.001/1.002 for BCE, 0.01/1.02 for tarreg). Using one for both is a
//      plausible-looking slip no type checker sees.
//   3. The shared encoder's gradient drops one of the three heads.
//   4. The L2 penalty is added to the loss but not to the gradient (or the
//      reverse), which trains a network that is not the one being reported.
//
// So the gate is a central finite-difference check over EVERY parameter, plus
// two negative controls that must be able to turn it red, plus a structural
// assertion pinning each of the claims the loss doc makes.

///|
/// A tiny deterministic fixture. Small widths keep the exhaustive sweep cheap.
fn dn_fixture(
  n : Int,
  dim : Int,
  seed : UInt64,
) -> (@snn_mbt.DragonNet, Array[Float], Array[Float], Array[Float]) {
  let net = @snn_mbt.DragonNet::new(dim, width=6, half=4, seed=seed, std=0.2F)
  let rng = @snn_mbt.Xoshiro::new(seed + 7UL)
  let x : Array[Float] = @snn_mbt.rand_f32(rng, n * dim)
  let t : Array[Float] = @snn_mbt.rand_f32(rng, n)
  let y : Array[Float] = @snn_mbt.rand_f32(rng, n)
  (net, x, y, t)
}

///|
/// Total objective at `ratio`, without the L2 penalty.
fn dn_loss(
  net : @snn_mbt.DragonNet,
  x : Array[Float],
  y : Array[Float],
  t : Array[Float],
  n : Int,
  ratio : Float,
) -> Float {
  let out = @snn_mbt.dragonnet_forward(net, x, n)
  @snn_mbt.dragonnet_loss(out, y, t, n, ratio)
}

///|
/// The full objective `dragonnet_backward` is meant to differentiate: the
/// data loss plus the same L2 penalty that `dragonnet_l2_penalty` reports.
fn dn_objective(
  net : @snn_mbt.DragonNet,
  x : Array[Float],
  y : Array[Float],
  t : Array[Float],
  n : Int,
  ratio : Float,
  l2 : Float,
) -> Float {
  dn_loss(net, x, y, t, n, ratio) +
  @snn_mbt.dragonnet_l2_penalty(net, l2)
}

///|
/// Full parameter gradient, flattened in `dragonnet_flatten` order.
fn dn_grad_flat(
  net : @snn_mbt.DragonNet,
  x : Array[Float],
  y : Array[Float],
  t : Array[Float],
  n : Int,
  ratio : Float,
  l2 : Float,
) -> Array[Float] {
  let out = @snn_mbt.dragonnet_forward(net, x, n)
  let (d0, d1, dt, de) = @snn_mbt.dragonnet_loss_grads(out, y, t, n, ratio)
  let g = @snn_mbt.dragonnet_backward(net, x, out, d0, d1, dt, de, n, l2)
  @snn_mbt.dragonnet_grad_flatten(g)
}

///|
/// The gradient's epsilon entry, which lives at the end of the flat vector.
fn dn_eps_index(net : @snn_mbt.DragonNet) -> Int {
  @snn_mbt.dragonnet_param_count(net) - 1
}

///|
/// Scale-free relative difference. The absolute floor matters: a coordinate
/// whose true gradient is ~0 and whose finite difference is ~1e-6 is a pass,
/// not a 100x failure.
fn dn_rel(fd : Float, an : Float) -> Float {
  let d = fd - an
  let ad = if d < 0.0F { -d } else { d }
  let s1 = if fd < 0.0F { -fd } else { fd }
  let s2 = if an < 0.0F { -an } else { an }
  let m = s1 + s2
  if m < 1.0e-3F {
    ad
  } else {
    ad / m
  }
}

///|
/// Difference step for the sweep. Larger than it looks like it should be:
/// the objective is O(50), so one Float32 ulp there is ~6.5e-6, and two of
/// them divided by `2h` IS the finite-difference noise floor. At h = 4e-3
/// that floor is ~3e-3, which is comparable to the true gradient of a small
/// coordinate -- so the comparison must carry an absolute term or it is
/// really a comparison against rounding.
const DN_H : Float = 4.0e-3F

///|
/// Relative tolerance on the analytic gradient.
const DN_RTOL : Float = 2.0e-2F

///|
/// Absolute tolerance = ~3x the finite-difference noise floor at `DN_H`.
/// See `DN_H` for the derivation.
const DN_ATOL : Float = 1.0e-2F

///|
/// How far past the combined tolerance this coordinate sits. `> 0` means the
/// analytic gradient and the finite difference disagree by more than the
/// objective's own precision can explain.
fn dn_excess(fd : Float, an : Float) -> Float {
  let d = fd - an
  let ad = if d < 0.0F { -d } else { d }
  let m = if an < 0.0F { -an } else { an }
  ad - DN_RTOL * m
}

///|
/// Exhaustive central-difference sweep over every parameter.
/// Returns `(max_excess, worst_index, worst_fd, worst_an)`. A MAX over
/// coordinates, not a mean: one wrong coordinate hidden by 200 right ones is
/// still a bug.
fn dn_fd_sweep(
  net : @snn_mbt.DragonNet,
  x : Array[Float],
  y : Array[Float],
  t : Array[Float],
  n : Int,
  ratio : Float,
  l2 : Float,
  h : Float,
) -> (Float, Int, Float, Float) {
  let g = dn_grad_flat(net, x, y, t, n, ratio, l2)
  let base = @snn_mbt.dragonnet_flatten(net)
  let mut worst = -1.0e30F
  let mut worst_k = -1
  let mut worst_fd = 0.0F
  let mut worst_an = 0.0F
  let mut k = 0
  while k < base.length() {
    let up = base.copy()
    up[k] = base[k] + h
    let dnw = base.copy()
    dnw[k] = base[k] - h
    let l_up = dn_objective(
      @snn_mbt.dragonnet_unflatten(net, up), x, y, t, n, ratio, l2,
    )
    let l_dn = dn_objective(
      @snn_mbt.dragonnet_unflatten(net, dnw), x, y, t, n, ratio, l2,
    )
    let fd = (l_up - l_dn) / (2.0F * h)
    let e = dn_excess(fd, g[k])
    if e > worst {
      worst = e
      worst_k = k
      worst_fd = fd
      worst_an = g[k]
    }
    k = k + 1
  }
  (worst, worst_k, worst_fd, worst_an)
}

///|
test "flatten/unflatten round-trips and reports the right count" {
  let n = 4
  let (net, _x, _y, _t) = dn_fixture(n, 3, 13UL)
  let flat = @snn_mbt.dragonnet_flatten(net)
  let cnt = @snn_mbt.dragonnet_param_count(net)
  if flat.length() != cnt {
    fail("flatten length \{flat.length()} != param_count \{cnt}")
  }
  let back = @snn_mbt.dragonnet_flatten(
    @snn_mbt.dragonnet_unflatten(net, flat),
  )
  for i in 0..<cnt {
    if flat[i] != back[i] {
      fail("round trip differs at \{i}: \{flat[i]} vs \{back[i]}")
    }
  }
  // epsilon is last, and must be the net's own value
  if flat[cnt - 1] != net.epsilon {
    fail("last flat entry is not epsilon")
  }
}

///|
/// The ten layers, in the same order `dragonnet_flatten` uses. Needed because
/// `DragonNet` is `pub(all)`, so the test can read each layer's own arrays and
/// check them against its slice of the flat vector.
fn dn_layer_params(
  net : @snn_mbt.DragonNet,
) -> Array[@snn_mbt.LinearParam] {
  [
    net.enc1,
    net.enc2,
    net.enc3,
    net.y0_h1,
    net.y0_h2,
    net.y0_out,
    net.y1_h1,
    net.y1_h2,
    net.y1_out,
    net.t_head,
  ]
}

///|
/// `dragonnet_layer_offset` is what lets a caller address one layer without
/// re-deriving the layout, so it has to be right for all ten layers, not just
/// the first. An offset that is off by one does not crash -- it silently
/// hands the caller a neighbour's parameters, which is how a "blind one head"
/// control ended up blinding `t_head` instead.
test "layer offsets tile the flat vector exactly, bias then weight" {
  let (net, _x, _y, _t) = dn_fixture(5, 4, 19UL)
  let flat = @snn_mbt.dragonnet_flatten(net)
  let cnt = @snn_mbt.dragonnet_param_count(net)
  let layers = dn_layer_params(net)
  if @snn_mbt.dragonnet_layer_offset(net, 0) != 0 {
    fail("layer 0 must start at 0")
  }
  // Layers must tile the vector with no gap and no overlap.
  let mut cursor = 0
  for layer in 0..<10 {
    let lo = @snn_mbt.dragonnet_layer_offset(net, layer)
    if lo != cursor {
      fail(
        "layer \{layer}: offset \{lo} but the previous layer ended at \{cursor}",
      )
    }
    let hi = @snn_mbt.dragonnet_layer_offset(net, layer + 1)
    let p = layers[layer]
    if hi - lo != p.bias.length() + p.weight.length() {
      fail(
        "layer \{layer}: block is \{hi - lo} wide, layer has \{p.bias.length()} + \{p.weight.length()}",
      )
    }
    // and the contents must be bias first, then weight
    for i in 0..<p.bias.length() {
      if flat[lo + i] != p.bias[i] {
        let got = flat[lo + i]
        let want = p.bias[i]
        fail("layer \{layer} bias \{i}: flat \{got} != \{want}")
      }
    }
    for i in 0..<p.weight.length() {
      let at = lo + p.bias.length() + i
      if flat[at] != p.weight[i] {
        let got = flat[at]
        let want = p.weight[i]
        fail("layer \{layer} weight \{i}: flat \{got} != \{want}")
      }
    }
    cursor = hi
  }
  // epsilon is the LAST element, not one past the end
  let eps = @snn_mbt.dragonnet_layer_offset(net, 10)
  if eps != cnt - 1 {
    fail("epsilon offset is \{eps}, expected \{cnt - 1} (count \{cnt})")
  }
  if flat[eps] != net.epsilon {
    fail("flat[epsilon offset] is not epsilon")
  }
}

///|
test "elu forward and backward agree with finite differences" {
  let xs : Array[Float] = [-2.0F, -0.5F, 0.0F, 0.25F, 1.5F, 3.0F]
  let d : Array[Float] = [1.0F, -2.0F, 0.5F, 3.0F, -1.0F, 0.75F]
  let up = @snn_mbt.elu_backward(xs, d)
  let h = 1.0e-3F
  // Difference a SCALAR objective `L(x) = sum_j d_j * elu(x)_j`, not the
  // bare derivative. `elu_forward(x2)[i]` alone differences to `elu'(x_i)`,
  // which is NOT what elu_backward returns -- it returns `d_i * elu'(x_i)`,
  // so comparing the two fails by a factor of `d_i` and looks like a sign
  // bug when it is a mistake in the test.
  for i in 0..<xs.length() {
    let x2 = xs.copy()
    x2[i] = xs[i] + h
    let x1 = xs.copy()
    x1[i] = xs[i] - h
    let l2 = @snn_mbt.elu_forward(x2)
    let l1 = @snn_mbt.elu_forward(x1)
    let mut a2 = 0.0F
    let mut a1 = 0.0F
    for j in 0..<xs.length() {
      a2 = a2 + d[j] * l2[j]
      a1 = a1 + d[j] * l1[j]
    }
    let fd = (a2 - a1) / (2.0F * h)
    let rel = dn_rel(fd, up[i])
    if rel > 1.0e-2F {
      fail("elu_backward[\{i}]: fd=\{fd} analytic=\{up[i]} rel=\{rel}")
    }
  }
}

///|
test "elu is identity above zero and alpha*(e^x-1) below" {
  let xs : Array[Float] = [-2.0F, -0.5F, 0.0F, 0.25F, 1.5F]
  let out = @snn_mbt.elu_forward(xs)
  for i in 0..<xs.length() {
    let want = if xs[i] > 0.0F { xs[i] } else { @snn_mbt.expf(xs[i]) - 1.0F }
    if (out[i] - want).abs() > 1.0e-6F {
      fail("elu(\{xs[i]}) = \{out[i]}, expected \{want}")
    }
  }
}

///|


///|

///|
test "every gradient matches central differences, tarreg ratio 1" {
  let (net, x, y, t) = dn_fixture(7, 5, 11UL)
  let (ex, k, fd, an) = dn_fd_sweep(net, x, y, t, 7, 1.0F, 0.0F, DN_H)
  if ex > DN_ATOL {
    fail(
      "excess \{ex} (tol \{DN_ATOL}) at flat index \{k}: fd=\{fd} analytic=\{an}",
    )
  }
}

///|
test "every gradient matches central differences, vanilla ratio 0" {
  let (net, x, y, t) = dn_fixture(6, 4, 23UL)
  let (ex, k, fd, an) = dn_fd_sweep(net, x, y, t, 6, 0.0F, 0.0F, DN_H)
  if ex > DN_ATOL {
    fail(
      "excess \{ex} (tol \{DN_ATOL}) at flat index \{k}: fd=\{fd} analytic=\{an}",
    )
  }
}

///|
test "the l2 penalty is on the outcome heads only, and reaches the gradient" {
  let n = 6
  let dim = 4
  let l2 = 0.03F
  let (net, x, y, t) = dn_fixture(n, dim, 31UL)
  // The penalty must change the objective ...
  let before = dn_objective(net, x, y, t, n, 1.0F, 0.0F)
  let after = dn_objective(net, x, y, t, n, 1.0F, l2)
  if !(after > before) {
    fail("l2 penalty did not raise the objective: \{before} -> \{after}")
  }
  // ... and the gradient sweep must agree WITH it, which fails if the
  // penalty is missing from one side only.
  let (ex, k, fd, an) = dn_fd_sweep(net, x, y, t, n, 1.0F, l2, DN_H)
  if ex > DN_ATOL {
    fail(
      "with l2: excess \{ex} (tol \{DN_ATOL}) at flat index \{k}: fd=\{fd} analytic=\{an}",
    )
  }
  // And with l2 = 0 it must still agree, i.e. the two paths are not entangled.
  let (ex2, k2, fd2, an2) = dn_fd_sweep(net, x, y, t, n, 1.0F, 0.0F, DN_H)
  if ex2 > DN_ATOL {
    fail(
      "without l2: excess \{ex2} at flat index \{k2}: fd=\{fd2} analytic=\{an2}",
    )
  }
}

///|
test "epsilon has exactly zero gradient when ratio is 0" {
  let (net, x, y, t) = dn_fixture(6, 4, 41UL)
  let g = dn_grad_flat(net, x, y, t, 6, 0.0F, 0.0F)
  let de = g[dn_eps_index(net)]
  if de != 0.0F {
    fail("expected d_epsilon == 0 at ratio=0, got \{de}")
  }
}

///|
test "epsilon gradient is non-zero at ratio 1" {
  let (net, x, y, t) = dn_fixture(6, 4, 41UL)
  let g = dn_grad_flat(net, x, y, t, 6, 1.0F, 0.0F)
  let de = g[dn_eps_index(net)]
  if de == 0.0F {
    fail("d_epsilon is exactly zero at ratio=1; the tarreg path is dead")
  }
}

///|
/// Negative control #1: doubling d_epsilon must be caught. Without this, a
/// gate whose comparison silently stopped running would report PASS forever.
test "negative control: a doubled epsilon gradient IS detected" {
  let n = 6
  let (net, x, y, t) = dn_fixture(n, 4, 53UL)
  let g = dn_grad_flat(net, x, y, t, n, 1.0F, 0.0F)
  let ei = dn_eps_index(net)
  let good = g[ei]
  if good == 0.0F {
    fail("fixture gives a zero epsilon gradient, so the control is vacuous")
  }
  let h = 2.0e-3F
  let base = @snn_mbt.dragonnet_flatten(net)
  let up = base.copy()
  up[ei] = base[ei] + h
  let dnw = base.copy()
  dnw[ei] = base[ei] - h
  let fd = (dn_objective(@snn_mbt.dragonnet_unflatten(net, up), x, y, t, n, 1.0F, 0.0F) -
    dn_objective(@snn_mbt.dragonnet_unflatten(net, dnw), x, y, t, n, 1.0F, 0.0F)) /
    (2.0F * h)
  if dn_rel(fd, good) > 2.0e-2F {
    fail("control: the honest gradient already disagrees: fd=\{fd} vs \{good}")
  }
  if dn_rel(fd, good * 2.0F) < 5.0e-2F {
    fail("control: doubling d_epsilon was NOT detected -- the gate is blind")
  }
}

///|
/// Negative control #2: a sign flip on d_epsilon is the failure mode that
/// trains to a plausible, quietly-wrong answer. Prove it is caught.
test "negative control: a sign-flipped epsilon gradient IS detected" {
  let n = 6
  let (net, x, y, t) = dn_fixture(n, 4, 53UL)
  let g = dn_grad_flat(net, x, y, t, n, 1.0F, 0.0F)
  let ei = dn_eps_index(net)
  let h = 2.0e-3F
  let base = @snn_mbt.dragonnet_flatten(net)
  let up = base.copy()
  up[ei] = base[ei] + h
  let dnw = base.copy()
  dnw[ei] = base[ei] - h
  let fd = (dn_objective(@snn_mbt.dragonnet_unflatten(net, up), x, y, t, n, 1.0F, 0.0F) -
    dn_objective(@snn_mbt.dragonnet_unflatten(net, dnw), x, y, t, n, 1.0F, 0.0F)) /
    (2.0F * h)
  if dn_rel(fd, -g[ei]) < 5.0e-2F {
    fail("control: a sign flip on d_epsilon was NOT detected")
  }
}

///|
/// Semi-synthetic data where the treatment effect is a known constant and
/// x[0] confounds both treatment and outcome.
fn dn_semisynth(
  n : Int,
  dim : Int,
  ate : Float,
  seed : UInt64,
) -> (Array[Float], Array[Float], Array[Float]) {
  let rng = @snn_mbt.Xoshiro::new(seed)
  let x : Array[Float] = @snn_mbt.rand_f32(rng, n * dim)
  let u : Array[Float] = @snn_mbt.rand_f32(rng, n)
  let v : Array[Float] = @snn_mbt.rand_f32(rng, n)
  let t : Array[Float] = Array::make(n, 0.0F)
  let y : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let z = x[i * dim] * 1.2F
    let p = 1.0F / (1.0F + @snn_mbt.expf(-z))
    let ti = if u[i] < p { 1.0F } else { 0.0F }
    t[i] = ti
    y[i] = 0.2F * x[i * dim] + (v[i] - 0.5F) + ate * ti
  }
  (x, y, t)
}

///|
/// Train full-batch for a fixed number of Adam steps and report
/// `(first_loss, last_loss, ate_estimate)`.
fn dn_train(
  net : @snn_mbt.DragonNet,
  x : Array[Float],
  y : Array[Float],
  t : Array[Float],
  n : Int,
  steps : Int,
  ratio : Float,
  l2 : Float,
) -> (Float, Float, Float) {
  let first = dn_objective(net, x, y, t, n, ratio, l2)
  let mut cur = net
  let mut opt = @snn_mbt.DragonNetAdam::new(cur)
  let mut i = 0
  while i < steps {
    let out = @snn_mbt.dragonnet_forward(cur, x, n)
    let (d0, d1, dt, de) = @snn_mbt.dragonnet_loss_grads(out, y, t, n, ratio)
    let g = @snn_mbt.dragonnet_backward(cur, x, out, d0, d1, dt, de, n, l2)
    let (cur2, opt2) = @snn_mbt.dragonnet_adam_step(
      cur, g, opt, 0.01F, 0.9F, 0.999F, 1.0e-8F,
    )
    cur = cur2
    opt = opt2
    i = i + 1
  }
  let last = dn_objective(cur, x, y, t, n, ratio, l2)
  let out = @snn_mbt.dragonnet_forward(cur, x, n)
  let mut s1 = 0.0F
  let mut s0 = 0.0F
  for k in 0..<n {
    s1 = s1 + out.y1[k]
    s0 = s0 + out.y0[k]
  }
  let est = (s1 - s0) / Float::from_int(n)
  (first, last, est)
}

///|
test "training reduces the objective and recovers the ATE" {
  let n = 128
  let dim = 4
  let ate = 2.0F
  let (x, y, t) = dn_semisynth(n, dim, ate, 97UL)
  let net = @snn_mbt.DragonNet::new(dim, width=16, half=8, seed=5UL, std=0.1F)
  let (first, last, est) = dn_train(net, x, y, t, n, 150, 1.0F, 0.01F)
  if !(last < first) {
    fail("objective did not decrease: \{first} -> \{last}")
  }
  if est <= 0.0F {
    fail("ATE estimate \{est} has the wrong sign (truth \{ate})")
  }
  let err = if est < ate { ate - est } else { est - ate }
  if err > 1.0F {
    fail("ATE estimate \{est} is more than 1.0 from truth \{ate}")
  }
}

///|
/// The negative control for the training test: a model that cannot see the
/// treatment at all must NOT recover the ATE. Without this, a gate that
/// "recovers ATE" because the data leaked the answer would pass.
test "control: with the outcomes shuffled there is no effect to find" {
  let n = 128
  let dim = 4
  let ate = 2.0F
  let (x, y, t) = dn_semisynth(n, dim, ate, 97UL)
  // Reverse y: same covariates, same treatment, same marginal outcome
  // distribution, but y no longer depends on t, so the true ATE is 0 and the
  // conditional means in both arms are equal by construction.
  //
  // An earlier version of this control zeroed the y1 head's parameters
  // instead. That is a weaker control -- the targeted-regularisation term can
  // still drag y0 down uniformly, which is exactly what happened: the
  // "blinded" model reported an ATE of 2.18 and the control failed for a
  // reason that had nothing to do with a leak. Shuffling the outcomes
  // removes the signal rather than the head that reads it.
  let y_shuf : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    y_shuf[i] = y[n - 1 - i]
  }
  let net = @snn_mbt.DragonNet::new(dim, width=16, half=8, seed=5UL, std=0.1F)
  let (_f, _l, est) = dn_train(net, x, y_shuf, t, n, 150, 1.0F, 0.01F)
  let mag = if est < 0.0F { -est } else { est }
  if mag > 1.0F {
    fail(
      "control: shuffled outcomes still produced an ATE estimate of \{est}",
    )
  }
}

///|
test "the concat has the layout the Keras model returns" {
  let n = 3
  let width = 6
  let (net, x, _y, _t) = dn_fixture(n, 4, 61UL)
  let out = @snn_mbt.dragonnet_forward(net, x, n)
  let c = @snn_mbt.dragonnet_concat(out, n, width)
  let cols = 4 + width
  if c.length() != n * cols {
    fail("concat length \{c.length()} != \{n * cols}")
  }
  for b in 0..<n {
    let o = b * cols
    if c[o] != out.y0[b] || c[o + 1] != out.y1[b] || c[o + 2] != out.t_hat[b] {
      fail("concat head mismatch at row \{b}")
    }
    if c[o + 3] != out.epsilon {
      fail("concat epsilon mismatch at row \{b}")
    }
    for k in 0..<width {
      if c[o + 4 + k] != out.phi[b * width + k] {
        fail("concat phi mismatch at row \{b} col \{k}")
      }
    }
  }
}

///|
test "the paper's 200-wide configuration builds, runs and stays finite" {
  let n = 5
  let dim = 3
  // Built directly, not via dn_fixture: that one is deliberately narrow, and
  // reusing it here checked the fixture's width rather than the default.
  let net = @snn_mbt.DragonNet::new(dim, seed=71UL)
  let rng = @snn_mbt.Xoshiro::new(78UL)
  let x : Array[Float] = @snn_mbt.rand_f32(rng, n * dim)
  let t : Array[Float] = @snn_mbt.rand_f32(rng, n)
  let y : Array[Float] = @snn_mbt.rand_f32(rng, n)
  let out = @snn_mbt.dragonnet_forward(net, x, n)
  if out.phi.length() != n * 200 {
    fail("phi width is \{out.phi.length() / n}, expected 200")
  }
  let l = dn_loss(net, x, y, t, n, 1.0F)
  if !(l == l) {
    fail("loss is NaN at the paper's width")
  }
  let g = dn_grad_flat(net, x, y, t, n, 1.0F, 0.01F)
  let ei = dn_eps_index(net)
  if g[ei] == 0.0F {
    fail("d_epsilon vanished at width 200")
  }
}