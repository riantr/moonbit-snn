// lstm_demo.mbt — LSTM sequence processing + training demo (v0.29.1).
//
// Sequence-level LSTM operations:
//   - `lstm_sequence_forward` — run an LSTM cell over an input sequence,
///     returning per-step hidden states and per-step caches for BPTT.
//   - `lstm_sequence_loss` — mean-squared-error loss between the
///     sequence of hidden states and a target sequence.
//   - `lstm_sequence_backward` — BPTT: walk caches in reverse, sum
///     parameter gradients, accumulate per-input gradients.
//   - `lstm_train_n_steps` — K-step SGD on a single sequence with
///     optional gradient clipping (mirrors scnn_train_n_steps).

///|
/// Run an LSTM cell over an input sequence. Returns
/// (hs, cs_final, caches) where:
///   - `hs[t]` is the hidden state at time t (length d_h)
///   - `cs_final` is the final cell state (length d_h)
///   - `caches` has length `seq_len` for BPTT.
pub fn lstm_sequence_forward(
  xs : Array[Array[Float]],
  h0 : Array[Float],
  c0 : Array[Float],
  param : LstmCellParam,
) -> (Array[Array[Float]], Array[Float], Array[LstmCellCache]) {
  let n = xs.length()
  let d_h = param.d_h
  let hs : Array[Array[Float]] = []
  let caches : Array[LstmCellCache] = []
  let mut cur_h : Array[Float] = h0
  let mut cur_c : Array[Float] = c0
  for t in 0..<n {
    let (h_t, cache) = lstm_cell_forward(xs[t], cur_h, cur_c, param)
    hs.push(h_t)
    caches.push(cache)
    cur_h = h_t
    cur_c = cache.c_t
  }
  let _ = d_h
  (hs, cur_c, caches)
}

///|
/// Mean-squared-error loss between predicted hidden states and a
/// target sequence (both length `n`, each entry length d_h).
pub fn lstm_sequence_loss(
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
/// (d_xs, d_h0, d_c0, grad).
///
/// Each `d_xs[t]` is the gradient w.r.t. `xs[t]`.
pub fn lstm_sequence_backward(
  predicted : Array[Array[Float]],
  target : Array[Array[Float]],
  caches : Array[LstmCellCache],
  h0 : Array[Float],
  c0 : Array[Float],
  param : LstmCellParam,
) -> (Array[Array[Float]], Array[Float], Array[Float], LstmCellGrad) {
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
  let grad = LstmCellGrad::zero(param)
  // Walk caches in reverse. d_c starts at zero (no upstream).
  let d_c : Array[Float] = Array::make(d_h, 0.0F)
  // We need to track d_h propagation back. For t > 0 we have to add
  // d_h_prev to the previous step's d_h (which is d_xs[t-1]).
  let d_h_prev_global : Array[Float] = Array::make(d_h, 0.0F)
  for t_step in 0..<n {
    let t = n - 1 - t_step
    let cache = caches[t]
    // Add upstream d_h (from t > 0 propagation) to d_xs[t] before
    // running the cell backward.
    let d_xs_t : Array[Float] = d_xs[t]
    if t > 0 {
      for i in 0..<d_h {
        d_xs_t[i] = d_xs_t[i] + d_h_prev_global[i]
      }
    }
    let (d_h_prev, d_x_t, d_c_prev) = lstm_cell_backward(
      cache, d_xs_t, d_c, param, grad,
    )
    d_xs[t] = d_x_t
    for i in 0..<d_h {
      d_c[i] = d_c_prev[i]
      d_h_prev_global[i] = d_h_prev[i]
    }
  }
  // h_prev_global at the end of the loop is the gradient w.r.t. h0.
  // d_c at the end is the gradient w.r.t. c0.
  let d_h0 = d_h_prev_global
  let d_c0 = d_c
  let _ = h0
  let _ = c0
  let _ = d_x
  (d_xs, d_h0, d_c0, grad)
}

///|
/// Clamp gradient values to `[-clip, grad_clip]`. Modifies `grad`
/// in place (clips all entries).
pub fn lstm_clip_grad(grad : LstmCellGrad, clip : Float) -> Unit {
  let d_h = grad.d_w_f.length()
  for i in 0..<d_h {
    let in_dim = grad.d_w_f[i].length()
    for j in 0..<in_dim {
      grad.d_w_f[i][j] = lstm_clip_value(grad.d_w_f[i][j], clip)
      grad.d_w_i[i][j] = lstm_clip_value(grad.d_w_i[i][j], clip)
      grad.d_w_c[i][j] = lstm_clip_value(grad.d_w_c[i][j], clip)
      grad.d_w_o[i][j] = lstm_clip_value(grad.d_w_o[i][j], clip)
    }
    grad.d_b_f[i] = lstm_clip_value(grad.d_b_f[i], clip)
    grad.d_b_i[i] = lstm_clip_value(grad.d_b_i[i], clip)
    grad.d_b_c[i] = lstm_clip_value(grad.d_b_c[i], clip)
    grad.d_b_o[i] = lstm_clip_value(grad.d_b_o[i], clip)
  }
}

fn lstm_clip_value(x : Float, clip : Float) -> Float {
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
pub fn lstm_train_n_steps(
  xs : Array[Array[Float]],
  target : Array[Array[Float]],
  h0 : Array[Float],
  c0 : Array[Float],
  param : LstmCellParam,
  n_steps : Int,
  lr : Float,
  clip : Float,
) -> Float {
  let mut last_loss = 0.0F
  for _step in 0..<n_steps {
    let (hs, _cs, caches) = lstm_sequence_forward(xs, h0, c0, param)
    let loss = lstm_sequence_loss(hs, target)
    let (_d_xs, _d_h0, _d_c0, grad) = lstm_sequence_backward(
      hs, target, caches, h0, c0, param,
    )
    if clip > 0.0F {
      lstm_clip_grad(grad, clip)
    }
    lstm_cell_sgd_step(param, grad, lr)
    last_loss = loss
  }
  last_loss
}

///|
/// Generate a simple synthetic sequence-prediction task: the target
/// at time t+1 is a delayed version of input at time t (i.e., learn
/// the identity over a 1-step shift). Returns (xs, target) both
/// length `seq_len`, each entry length `d_x`.
pub fn lstm_identity_dataset(
  seq_len : Int,
  d_x : Int,
  seed : UInt64,
) -> (Array[Array[Float]], Array[Array[Float]]) {
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let xs : Array[Array[Float]] = Array::make(seq_len, [])
  let target : Array[Array[Float]] = Array::make(seq_len, [])
  for t in 0..<seq_len {
    let x : Array[Float] = Array::make(d_x, 0.0F)
    let g : Array[Float] = Array::make(d_x, 0.0F)
    for i in 0..<d_x {
      let (z1, _) = box_muller(rng)
      x[i] = Float::from_double(z1)
      // target[t] = xs[(t-1) mod seq_len] (cyclic shift)
      let prev = if t > 0 { xs[t - 1] } else { x }
      g[i] = prev[i]
    }
    xs[t] = x
    target[t] = g
  }
  (xs, target)
}