// gru_demo.mbt — GRU sequence processing + training demo (v0.30.1).
//
// Sequence-level GRU operations:
//   - `gru_sequence_forward` — run a GRU cell over an input sequence,
//     returning per-step hidden states and per-step caches for BPTT.
//   - `gru_sequence_loss` — mean-squared-error loss between the
//     sequence of hidden states and a target sequence.
//   - `gru_sequence_backward` — BPTT: walk caches in reverse, sum
//     parameter gradients, accumulate per-input gradients.
//   - `gru_train_n_steps` — K-step SGD on a single sequence with
//     optional gradient clipping (mirrors lstm_train_n_steps).
//   - `gru_identity_dataset` — synthetic identity-shift task with
//     separate input dim and target dim.

///|
/// Run a GRU cell over an input sequence. Returns
/// (hs, caches) where:
///   - `hs[t]` is the hidden state at time t (length d_h)
///   - `caches` has length `seq_len` for BPTT.
pub fn gru_sequence_forward(
  xs : Array[Array[Float]],
  h0 : Array[Float],
  param : GruCellParam,
) -> (Array[Array[Float]], Array[GruCellCache]) {
  let n = xs.length()
  let hs : Array[Array[Float]] = []
  let caches : Array[GruCellCache] = []
  let mut cur_h : Array[Float] = h0
  for t in 0..<n {
    let (h_t, cache) = gru_cell_forward(xs[t], cur_h, param)
    hs.push(h_t)
    caches.push(cache)
    cur_h = h_t
  }
  (hs, caches)
}

///|
/// Mean-squared-error loss between predicted hidden states and a
/// target sequence (both length `n`, each entry length d_h).
pub fn gru_sequence_loss(
  predicted : Array[Array[Float]],
  target : Array[Array[Float]],
) -> Float {
  let n = predicted.length()
  if n == 0 {
    return 0.0F
  }
  if target.length() != n {
    return -1.0F
  }
  let mut sum = 0.0F
  let mut count = 0
  for t in 0..<n {
    let p = predicted[t]
    let g = target[t]
    if p.length() != g.length() {
      return -1.0F
    }
    for i in 0..<p.length() {
      let d = p[i] - g[i]
      sum = sum + d * d
      count = count + 1
    }
  }
  if count == 0 {
    return 0.0F
  }
  sum / Float::from_int(count)
}

///|
/// BPTT backward: given predicted, target, and caches, returns
/// (d_xs, d_h0, grad).
pub fn gru_sequence_backward(
  predicted : Array[Array[Float]],
  target : Array[Array[Float]],
  caches : Array[GruCellCache],
  h0 : Array[Float],
  param : GruCellParam,
) -> (Array[Array[Float]], Array[Float], GruCellGrad) {
  let n = predicted.length()
  let d_h = param.d_h
  let d_x = param.d_x
  // d_loss / d_h_t = 2 (h_t - target_t) / count
  let count = n * d_h
  let scale = 2.0F / Float::from_int(count)
  let d_xs : Array[Array[Float]] = Array::make(n, [])
  for t in 0..<n {
    let d_h_t : Array[Float] = Array::make(d_h, 0.0F)
    for i in 0..<d_h {
      d_h_t[i] = scale * (predicted[t][i] - target[t][i])
    }
    d_xs[t] = d_h_t
  }
  let grad = GruCellGrad::zero(param)
  // Walk caches in reverse. d_h_prev starts at zero.
  let d_h_prev_global : Array[Float] = Array::make(d_h, 0.0F)
  for t_step in 0..<n {
    let t = n - 1 - t_step
    let cache = caches[t]
    let d_hs_t : Array[Float] = d_xs[t]
    if t > 0 {
      for i in 0..<d_h {
        d_hs_t[i] = d_hs_t[i] + d_h_prev_global[i]
      }
    }
    let (d_h_prev, d_x_t) = gru_cell_backward(cache, d_hs_t, param, grad)
    d_xs[t] = d_x_t
    for i in 0..<d_h {
      d_h_prev_global[i] = d_h_prev[i]
    }
  }
  let d_h0 = d_h_prev_global
  let _ = h0
  let _ = d_x
  (d_xs, d_h0, grad)
}

///|
/// Clamp gradient values to `[-clip, grad_clip]`. Modifies `grad`
/// in place (clips all entries).
pub fn gru_clip_grad(grad : GruCellGrad, clip : Float) -> Unit {
  let d_h = grad.d_w_z.length()
  for i in 0..<d_h {
    let in_dim = grad.d_w_z[i].length()
    for j in 0..<in_dim {
      grad.d_w_z[i][j] = gru_clip_value(grad.d_w_z[i][j], clip)
      grad.d_w_r[i][j] = gru_clip_value(grad.d_w_r[i][j], clip)
      grad.d_w_n[i][j] = gru_clip_value(grad.d_w_n[i][j], clip)
    }
    grad.d_b_z[i] = gru_clip_value(grad.d_b_z[i], clip)
    grad.d_b_r[i] = gru_clip_value(grad.d_b_r[i], clip)
    grad.d_b_n[i] = gru_clip_value(grad.d_b_n[i], clip)
  }
}

fn gru_clip_value(x : Float, clip : Float) -> Float {
  if x > clip {
    return clip
  }
  if x < -clip {
    return -clip
  }
  x
}

///|
/// Run K-step SGD training on a single input/target sequence. Returns
/// the final loss. Optionally clips gradients to `[-clip, clip]`.
pub fn gru_train_n_steps(
  xs : Array[Array[Float]],
  target : Array[Array[Float]],
  h0 : Array[Float],
  param : GruCellParam,
  n_steps : Int,
  lr : Float,
  clip : Float,
) -> Float {
  let mut last_loss = 0.0F
  for _step in 0..<n_steps {
    let (hs, caches) = gru_sequence_forward(xs, h0, param)
    let loss = gru_sequence_loss(hs, target)
    let (_d_xs, _d_h0, grad) = gru_sequence_backward(hs, target, caches, h0, param)
    if clip > 0.0F {
      gru_clip_grad(grad, clip)
    }
    gru_cell_sgd_step(param, grad, lr)
    last_loss = loss
  }
  last_loss
}

///|
/// Generate a simple synthetic sequence-prediction task: the target
/// at time t+1 is a delayed version of input at time t (i.e., learn
/// the identity over a 1-step shift). xs has dim `d_x`; target has
/// dim `d_target`.
pub fn gru_identity_dataset(
  seq_len : Int,
  d_x : Int,
  d_target : Int,
  seed : UInt64,
) -> (Array[Array[Float]], Array[Array[Float]]) {
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let xs : Array[Array[Float]] = Array::make(seq_len, [])
  let target : Array[Array[Float]] = Array::make(seq_len, [])
  for t in 0..<seq_len {
    let x : Array[Float] = Array::make(d_x, 0.0F)
    let g : Array[Float] = Array::make(d_target, 0.0F)
    for i in 0..<d_x {
      let (z1, _) = box_muller(rng)
      x[i] = Float::from_double(z1)
    }
    for i in 0..<d_target {
      let (z1, _) = box_muller(rng)
      g[i] = Float::from_double(z1)
    }
    xs[t] = x
    target[t] = g
  }
  // target[t] = xs[(t-1) mod n] in its first min(d_x, d_target) entries.
  for t in 0..<seq_len {
    let prev_idx = if t > 0 { t - 1 } else { seq_len - 1 }
    let prev = xs[prev_idx]
    let overlap = if d_x < d_target { d_x } else { d_target }
    for i in 0..<overlap {
      target[t][i] = prev[i]
    }
  }
  (xs, target)
}
