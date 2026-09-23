// bilstm.mbt — Bidirectional LSTM (v0.32.1).
//
// Runs two LSTMs over the input:
//   - Forward:  standard left-to-right
//   - Backward: right-to-left
//
// Output hidden state at time t is the concat
//   h_combined[t] = [h_f[t]; h_b[t]]
// of length 2 * d_h. The loss is on h_combined.
//
// Backward:
//   d_h_combined[t] -> d_h_f[t] (first d_h) + d_h_b[t] (last d_h)
//   - Forward LSTM BPTT walks t = T-1, ..., 0.
//   - Backward LSTM BPTT walks t = 0, ..., T-1 (i.e., reverses time).
//   Both contribute to d_xs[t] which is summed.

///|
/// Parameter bundle for a 1-layer BiLSTM. Each direction is its
/// own LstmCellParam.
pub struct BiLstmParam {
  d_x : Int
  d_h : Int
  fwd : LstmCellParam
  bwd : LstmCellParam
}

///|
/// Build a 1-layer BiLSTM with both directions having hidden dim
/// `d_h`. Forward uses seed `seed`, backward uses `seed + 1`.
pub fn BiLstmParam::new(d_x : Int, d_h : Int, seed : UInt64) -> BiLstmParam {
  {
    d_x,
    d_h,
    fwd: LstmCellParam::new(d_x, d_h, seed),
    bwd: LstmCellParam::new(d_x, d_h, seed + 1UL),
  }
}

///|
/// Forward cache storing per-direction per-step cell caches plus
/// the forward outputs (used by BPTT and for loss computation).
pub struct BiLstmCache {
  caches_f : Array[LstmCellCache]
  caches_b : Array[LstmCellCache]
  hs_f : Array[Array[Float]]
  hs_b : Array[Array[Float]]
  cs_f : Array[Array[Float]]
  cs_b : Array[Array[Float]]
}

///|
/// Run the BiLSTM forward over an input sequence. Returns
/// `(hs_f, hs_b, cs_f, cs_b, cache)`. `hs_f[t]` and `hs_b[t]` are
/// the per-direction hidden states (each length d_h).
pub fn bilstm_forward(
  xs : Array[Array[Float]],
  h0_f : Array[Float],
  c0_f : Array[Float],
  h0_b : Array[Float],
  c0_b : Array[Float],
  param : BiLstmParam,
) -> (Array[Array[Float]], Array[Array[Float]], Array[Array[Float]], Array[Array[Float]], BiLstmCache) {
  let n = xs.length()
  let hs_f : Array[Array[Float]] = []
  let cs_f : Array[Array[Float]] = []
  let caches_f : Array[LstmCellCache] = []
  let hs_b : Array[Array[Float]] = []
  let cs_b : Array[Array[Float]] = []
  let caches_b : Array[LstmCellCache] = []
  // Forward direction: left-to-right.
  let mut cur_h = h0_f
  let mut cur_c = c0_f
  for t in 0..<n {
    let (h_t, cache_t) = lstm_cell_forward(xs[t], cur_h, cur_c, param.fwd)
    hs_f.push(h_t)
    cs_f.push(cache_t.c_t)
    caches_f.push(cache_t)
    cur_h = h_t
    cur_c = cache_t.c_t
  }
  // Backward direction: right-to-left.
  cur_h = h0_b
  cur_c = c0_b
  let mut t = n - 1
  while t >= 0 {
    let (h_t, cache_t) = lstm_cell_forward(xs[t], cur_h, cur_c, param.bwd)
    hs_b.push(h_t)
    cs_b.push(cache_t.c_t)
    caches_b.push(cache_t)
    cur_h = h_t
    cur_c = cache_t.c_t
    t = t - 1
  }
  let cache : BiLstmCache = {
    caches_f, caches_b, hs_f, hs_b, cs_f, cs_b,
  }
  (hs_f, hs_b, cs_f, cs_b, cache)
}

///|
/// Build the concat hidden state `[hs_f[t]; hs_b[t]]` at time t.
pub fn bilstm_concat(hs_f : Array[Array[Float]], hs_b : Array[Array[Float]], t : Int) -> Array[Float] {
  let d_h = hs_f[t].length()
  let out : Array[Float] = Array::make(2 * d_h, 0.0F)
  for i in 0..<d_h {
    out[i] = hs_f[t][i]
    out[d_h + i] = hs_b[t][i]
  }
  out
}

///|
/// Mean-squared-error loss on concat hidden states vs target.
pub fn bilstm_sequence_loss(
  hs_f : Array[Array[Float]],
  hs_b : Array[Array[Float]],
  target : Array[Array[Float]],
) -> Float {
  let n = hs_f.length()
  if n == 0 {
    return 0.0F
  }
  if target.length() != n {
    return -1.0F
  }
  let mut sum = 0.0F
  let mut count = 0
  for t in 0..<n {
    let p = bilstm_concat(hs_f, hs_b, t)
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
/// BPTT backward for the BiLSTM. Returns
/// `(d_xs, d_h0_f, d_c0_f, d_h0_b, d_c0_b, grad_f, grad_b)`.
///
/// Forward BPTT walks t = T-1, ..., 0 with the forward cache.
/// Backward BPTT walks t = 0, ..., T-1 with the backward cache.
/// d_xs[t] accumulates contributions from both directions.
pub fn bilstm_backward(
  hs_f : Array[Array[Float]],
  hs_b : Array[Array[Float]],
  target : Array[Array[Float]],
  cache : BiLstmCache,
  h0_f : Array[Float],
  c0_f : Array[Float],
  h0_b : Array[Float],
  c0_b : Array[Float],
  param : BiLstmParam,
) -> (Array[Array[Float]], Array[Float], Array[Float], Array[Float], Array[Float], LstmCellGrad, LstmCellGrad) {
  let n = hs_f.length()
  let d_h = param.d_h
  let d_x = param.d_x
  let two_dh = 2 * d_h
  let count = n * two_dh
  let scale = 2.0F / Float::from_int(count)
  let d_xs : Array[Array[Float]] = Array::make(n, [])
  for t in 0..<n {
    d_xs[t] = Array::make(d_x, 0.0F)
  }
  let grad_f = LstmCellGrad::zero(param.fwd)
  let grad_b = LstmCellGrad::zero(param.bwd)
  // d_loss / d_concat_h[t] = scale * (concat - target)
  // d_h_f[t] = first d_h entries; d_h_b[t] = last d_h entries.
  let d_h_f_per_t : Array[Array[Float]] = Array::make(n, [])
  let d_h_b_per_t : Array[Array[Float]] = Array::make(n, [])
  for t in 0..<n {
    let p = bilstm_concat(hs_f, hs_b, t)
    let g = target[t]
    let dh_f : Array[Float] = Array::make(d_h, 0.0F)
    let dh_b : Array[Float] = Array::make(d_h, 0.0F)
    for i in 0..<d_h {
      dh_f[i] = scale * (p[i] - g[i])
      dh_b[i] = scale * (p[d_h + i] - g[d_h + i])
    }
    d_h_f_per_t[t] = dh_f
    d_h_b_per_t[t] = dh_b
  }
  // Forward LSTM BPTT (right to left in time).
  let d_h_prev_f : Array[Float] = Array::make(d_h, 0.0F)
  let d_c_f : Array[Float] = Array::make(d_h, 0.0F)
  for t_step in 0..<n {
    let t = n - 1 - t_step
    let d_hs_t : Array[Float] = zero_copy(d_h_f_per_t[t])
    if t < n - 1 {
      for i in 0..<d_h {
        d_hs_t[i] = d_hs_t[i] + d_h_prev_f[i]
      }
    }
    let (d_h_prev, d_x_t, _d_c_prev) = lstm_cell_backward(
      cache.caches_f[t], d_hs_t, d_c_f, param.fwd, grad_f,
    )
    for i in 0..<d_h {
      d_h_prev_f[i] = d_h_prev[i]
    }
    for i in 0..<d_x {
      d_xs[t][i] = d_xs[t][i] + d_x_t[i]
    }
  }
  // Backward LSTM BPTT: caches_b[k] holds the cache for internal
  // time k (which corresponds to actual time t = n-1-k, since the
  // forward pass pushed in reverse order). Walk internal time from
  // n-1 down to 0 so d_h_prev propagates correctly.
  let d_h_prev_b : Array[Float] = Array::make(d_h, 0.0F)
  let d_c_b : Array[Float] = Array::make(d_h, 0.0F)
  for k_step in 0..<n {
    let k = n - 1 - k_step
    let actual_t = n - 1 - k
    let d_hs_t : Array[Float] = zero_copy(d_h_b_per_t[actual_t])
    if k < n - 1 {
      for i in 0..<d_h {
        d_hs_t[i] = d_hs_t[i] + d_h_prev_b[i]
      }
    }
    let (d_h_prev, d_x_t, _d_c_prev) = lstm_cell_backward(
      cache.caches_b[k], d_hs_t, d_c_b, param.bwd, grad_b,
    )
    for i in 0..<d_h {
      d_h_prev_b[i] = d_h_prev[i]
    }
    for i in 0..<d_x {
      d_xs[actual_t][i] = d_xs[actual_t][i] + d_x_t[i]
    }
  }
  let _ = h0_f
  let _ = c0_f
  let _ = h0_b
  let _ = c0_b
  (d_xs, d_h_prev_f, d_c_f, d_h_prev_b, d_c_b, grad_f, grad_b)
}

///|
/// Apply SGD to both directions.
pub fn bilstm_sgd_step(
  param : BiLstmParam,
  grad_f : LstmCellGrad,
  grad_b : LstmCellGrad,
  lr : Float,
) -> Unit {
  lstm_cell_sgd_step(param.fwd, grad_f, lr)
  lstm_cell_sgd_step(param.bwd, grad_b, lr)
}

///|
/// Run K-step SGD training on a single input/target sequence.
pub fn bilstm_train_n_steps(
  xs : Array[Array[Float]],
  target : Array[Array[Float]],
  h0_f : Array[Float],
  c0_f : Array[Float],
  h0_b : Array[Float],
  c0_b : Array[Float],
  param : BiLstmParam,
  n_steps : Int,
  lr : Float,
  clip : Float,
) -> Float {
  let mut last_loss = 0.0F
  for _step in 0..<n_steps {
    let (hs_f, hs_b, _cs_f, _cs_b, cache) = bilstm_forward(
      xs, h0_f, c0_f, h0_b, c0_b, param,
    )
    let loss = bilstm_sequence_loss(hs_f, hs_b, target)
    let (_d_xs, _dh0_f, _dc0_f, _dh0_b, _dc0_b, grad_f, grad_b) = bilstm_backward(
      hs_f, hs_b, target, cache, h0_f, c0_f, h0_b, c0_b, param,
    )
    if clip > 0.0F {
      lstm_clip_grad(grad_f, clip)
      lstm_clip_grad(grad_b, clip)
    }
    bilstm_sgd_step(param, grad_f, grad_b, lr)
    last_loss = loss
  }
  last_loss
}

///|
/// Synthetic identity-shift task for BiLSTM. xs has dim d_x;
/// target has dim 2 * d_target (matching concat hidden dim).
pub fn bilstm_identity_dataset(
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
    for i in 0..<d_x {
      let (z1, _) = box_muller(rng)
      x[i] = Float::from_double(z1)
    }
    xs[t] = x
    let g : Array[Float] = Array::make(2 * d_target, 0.0F)
    target[t] = g
  }
  // target[t] = [xs[(t-1) mod n]; xs[(t+1) mod n]] in their overlap.
  for t in 0..<seq_len {
    let prev_idx = if t > 0 { t - 1 } else { seq_len - 1 }
    let next_idx = if t < seq_len - 1 { t + 1 } else { 0 }
    let prev = xs[prev_idx]
    let next = xs[next_idx]
    let overlap = if d_x < d_target { d_x } else { d_target }
    for i in 0..<overlap {
      target[t][i] = prev[i]
      target[t][d_target + i] = next[i]
    }
  }
  (xs, target)
}
