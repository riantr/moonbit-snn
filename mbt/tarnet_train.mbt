// tarnet_train.mbt -- one training step of the full model.
//
// Reference: arXiv:2605.07735 (ICME 2026), `tarnet/models/tarnet.py`.
//
// WHY ADAM IS REUSED RATHER THAN REWRITTEN: `adam_init(n, 0)` plus
// `adam_update_arrays(param, empty, grad, empty, ...)` treats the whole flat
// parameter vector as one "weight" array. That means the optimizer running here
// is the same already-gated code every other model in this package uses, so
// there is no second implementation to drift out of step with the first.

///|
/// One forward, one backward, one Adam update. Returns the updated model, the
/// updated optimizer state, and the loss BEFORE the update.
///
/// The loss is returned pre-update on purpose: a training loop that reports the
/// post-update loss makes its final number depend on a parameter set the
/// caller never evaluated.
pub fn tarnet_train_step(
  net : Tarnet,
  x : Array[Float],
  n : Int,
  targets : Array[Int],
  state : AdamState,
  lr : Float,
) -> (Tarnet, AdamState, Float) {
  let (out, cache) = tarnet_forward_full(net, x, n)
  let (d_logits, loss) = tarnet_softmax_dlogits(
    out.logits, n, net.n_classes, targets,
  )
  let (g, _d_x) = tarnet_backward(net, n, cache, d_logits, 0.0F)
  let cur = tarnet_flatten(net)
  let none : Array[Float] = Array::make(0, 0.0F)
  let (next, _none_out, st) = adam_update_arrays(
    cur, none, g, none, state, lr, 0.9F, 0.999F, 1.0e-8F,
  )
  (tarnet_unflatten(net, next), st, loss)
}

///|
/// Allocate optimizer state for a model's flat parameter vector.
pub fn tarnet_adam_init(net : Tarnet) -> AdamState {
  adam_init(tarnet_param_count(net), 0)
}

///|
/// Run `steps` full-batch steps and return the loss BEFORE each one, so
/// `history[0]` is the initial loss and `history[steps - 1]` is the last one
/// measured.
pub fn tarnet_train_loop(
  net : Tarnet,
  x : Array[Float],
  n : Int,
  targets : Array[Int],
  steps : Int,
  lr : Float,
) -> (Tarnet, Array[Float]) {
  let mut cur = net
  let mut state = tarnet_adam_init(net)
  let history : Array[Float] = Array::make(steps, 0.0F)
  let mut i = 0
  while i < steps {
    let (next, st, loss) = tarnet_train_step(cur, x, n, targets, state, lr)
    cur = next
    state = st
    history[i] = loss
    i = i + 1
  }
  (cur, history)
}

///|
/// Classification error rate of `net` on a batch.
pub fn tarnet_error_rate(
  net : Tarnet,
  x : Array[Float],
  n : Int,
  targets : Array[Int],
) -> Float {
  let out = tarnet_forward(net, x, n)
  let k = net.n_classes
  let mut wrong = 0
  let mut b = 0
  while b < n {
    let row = b * k
    let mut best = 0
    let mut bi = 1
    while bi < k {
      if out.logits[row + bi] > out.logits[row + best] {
        best = bi
      }
      bi = bi + 1
    }
    if best != targets[b] {
      wrong = wrong + 1
    }
    b = b + 1
  }
  Float::from_int(wrong) / Float::from_int(n)
}