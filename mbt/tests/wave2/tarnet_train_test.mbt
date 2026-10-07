// tarnet_train_test.mbt -- does the assembled model actually LEARN?
//
// Reference: arXiv:2605.07735 (ICME 2026).
//
// The gradient gates prove the derivative is correct. This one asks the
// question that matters to a user of this package: train it, and does the
// loss go down and the predictions become right?
//
// It is deliberately a whole-system gate rather than a unit test. Every stage
// has its own numerical gate; what none of those can catch is a training loop
// that reads the gradient in the wrong order, scales it twice, or reports a
// post-update loss and therefore looks fine forever.

///|
/// A batch that is genuinely learnable, so a gate failure means the model is
/// broken rather than the task being impossible.
///
/// Four samples, two classes: samples 0-1 carry a positive mean, samples 2-3 a
/// negative one. Nothing random about the inputs -- a task the model cannot do
/// would make this gate fail for the wrong reason.
fn tn_learnable_batch(n_classes : Int) -> (@snn_mbt.Tarnet, Array[Float], Array[Int]) {
  let n = 4
  let in_chan = 4
  let t = 6
  let net = @snn_mbt.Tarnet::new(
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
    emb_dim=8,
    att_hidden=8,
    seed=211UL,
  )
  let x : Array[Float] = Array::make(n * in_chan * t, 0.0F)
  let targets : Array[Int] = Array::make(n, 0)
  for b in 0..<n {
    let sign = if b < 2 { 1.0F } else { -1.0F }
    targets[b] = if b < 2 { 0 } else { 1 }
    for c in 0..<in_chan {
      for ti in 0..<t {
        let idx = (b * in_chan + c) * t + ti
        x[idx] = sign * (0.6F + 0.05F * Float::from_int(c + ti))
      }
    }
  }
  (net, x, targets)
}

test "Adam: the first step is exactly lr*g/(|g|+eps)" {
  // With t = 1 the bias correction cancels: m_hat = g and v_hat = g^2, so the
  // update is lr * g / (|g| + eps). Hand-checkable, so it pins the moment
  // update, the bias correction and the eps placement in one assertion.
  let lr = 0.01F
  let eps = 1.0e-8F
  let w : Array[Float] = [1.0F, -2.0F, 0.0F]
  let g : Array[Float] = [3.0F, -0.5F, 4.0F]
  let none : Array[Float] = Array::make(0, 0.0F)
  let state = @snn_mbt.adam_init(3, 0)
  let (w2, _none_out, _st) = @snn_mbt.adam_update_arrays(
    w, none, g, none, state, lr, 0.9F, 0.999F, eps,
  )
  let mut i = 0
  while i < 3 {
    let ag = if g[i] < 0.0F { -g[i] } else { g[i] }
    let want = w[i] - lr * g[i] / (ag + eps)
    if (w2[i] - want).abs() > 1.0e-6F {
      fail(
        "first Adam step[\{i}] = \{w2[i]}, expected \{want} (lr*g/(|g|+eps))",
      )
    }
    i = i + 1
  }
}

test "Tarnet: one train step moves exactly the parameters with a gradient" {
  // The gradient is already gated per coordinate; what is NOT gated is whether
  // the optimizer is handed THAT gradient, and whether the unflatten that puts
  // it back is the one the forward reads. A loop that updated a stale copy
  // would pass every gradient gate and never learn.
  //
  // The assertion is the PROPERTY, not a percentage. An earlier version
  // required 90% of the vector to move and failed at 3742/4359 -- correctly
  // flagging that some parameters have exactly zero gradient (dead PReLU
  // slopes, biases on dead channels) and must legitimately stay put. Demanding
  // a percentage would have meant loosening it until dead parameters looked
  // alive, which is the same mistake as widening a numeric tolerance to make
  // a test pass.
  let (net, x, targets) = tn_learnable_batch(2)
  let n = 4
  let state = @snn_mbt.tarnet_adam_init(net)
  let before = @snn_mbt.tarnet_flatten(net)
  // recompute the gradient this step will use
  let (out, cache) = @snn_mbt.tarnet_forward_full(net, x, n)
  let (d_logits, _l0) = @snn_mbt.tarnet_softmax_dlogits(
    out.logits, n, net.n_classes, targets,
  )
  let (g, _dx) = @snn_mbt.tarnet_backward(net, n, cache, d_logits, 0.0F)
  let (next, _st, loss0) = @snn_mbt.tarnet_train_step(
    net, x, n, targets, state, 0.01F,
  )
  let after = @snn_mbt.tarnet_flatten(next)
  if after.length() != before.length() || g.length() != before.length() {
    fail(
      "length mismatch across a training step: params \{before.length()} grad \{g.length()} after \{after.length()}",
    )
  }
  let mut dead = 0
  let mut moved = 0
  let mut missed = 0
  let mut first_missed = -1
  let mut i = 0
  while i < after.length() {
    if g[i] == 0.0F {
      dead = dead + 1
    } else {
      if after[i] != before[i] {
        moved = moved + 1
      } else {
        missed = missed + 1
        if first_missed < 0 {
          first_missed = i
        }
      }
    }
    i = i + 1
  }
  println(
    "train step: \{moved} moved, \{dead} zero-gradient, \{missed} live but unchanged",
  )
  if missed > 0 {
    fail(
      "\{missed} parameters had a non-zero gradient but did not move; first at index \{first_missed} -- the optimizer is not seeing the gradient this backward produced",
    )
  }
  if dead == 0 {
    fail(
      "no parameter had a zero gradient -- expected some (dead PReLU slopes and dead biases); the fixture may have changed",
    )
  }
  if moved == 0 {
    fail("no parameter moved at all")
  }
  if loss0 <= 0.0F {
    fail("initial loss \{loss0} must be positive")
  }
}

test "Tarnet: training drives the loss down and the error rate to zero" {
  // WHAT THIS GATE CANNOT SEE, because it is Adam's defining property: Adam's
  // update is lr * m_hat / (sqrt(v_hat) + eps), and rescaling the gradient by
  // any constant k rescales m_hat by k and sqrt(v_hat) by |k| -- they cancel.
  // Measured: halving the gradient before the update moved the 60-step loss
  // from 0.0006082716 to 0.0006083312 and every gate stayed green.
  //
  // So "does it train" is NOT a substitute for the per-coordinate numerical
  // gates; a gradient that is wrong by a constant factor trains perfectly well.
  // This gate earns its place by catching the wiring failures the numerical
  // gates cannot see -- a stale gradient, an update to a discarded copy, a
  // reported post-update loss. Zeroing the gradient fails it immediately (loss
  // frozen at 0.6388 for all 60 steps).
  let (net, x, targets) = tn_learnable_batch(2)
  let n = 4
  let steps = 60
  let (_final, history) = @snn_mbt.tarnet_train_loop(
    net, x, n, targets, steps, 0.02F,
  )
  let first = history[0]
  let last = history[steps - 1]
  println(
    "train: first=\{first} last=\{last} steps=\{steps}",
  )
  // The initial loss must be near log(2): the classifier bias is zero and the
  // embedding is freshly normalised, so an untrained model should be at chance.
  // If this assertion ever fails the task stopped being the test's to pass.
  if first < 0.5F || first > 0.9F {
    fail("initial loss \{first} is not near log(2) = 0.693 -- the fixture is not testing what it claims")
  }
  if last > first * 0.5F {
    fail(
      "after \{steps} steps the loss only fell from \{first} to \{last}; training is not working",
    )
  }
  // Monotone-ish overall, not strictly: a single noisy step can tick upward.
  let mut up_runs = 0
  let mut run = 0
  let mut i = 1
  while i < steps {
    if history[i] > history[i - 1] {
      run = run + 1
      if run > 3 {
        up_runs = up_runs + 1
      }
    } else {
      run = 0
    }
    i = i + 1
  }
  if up_runs > 0 {
    fail("the loss rose for more than 3 consecutive steps in \{up_runs} places -- training is unstable")
  }
  let err = @snn_mbt.tarnet_error_rate(_final, x, n, targets)
  if err > 0.0F {
    fail("error rate after training is \{err}, expected 0 on a separable batch")
  }
}