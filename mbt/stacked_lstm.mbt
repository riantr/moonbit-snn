// stacked_lstm.mbt — Multi-layer (stacked) LSTM (v0.32.0).
//
// Stacks L LSTM cells where the output hidden state of layer l
// becomes the input of layer l+1 (same timestep).
//
// Conventions:
//   - All layers share the same d_h.
//   - Layer 0 input dim = original d_x.
//   - Layer l > 0 input dim = d_h (same as previous layer output).
//   - The loss is on the top layer's hidden state.
//
// Forward (at each timestep t, layer l):
//   if l == 0: input_l = xs[t]
//   else:      input_l = h_t[l-1]
//   h_t[l], c_t[l] = lstm_cell_forward(input_l, h_{t-1}[l], c_{t-1}[l], params[l])
//
// Backward (BPTT through both layers and time):
//   Walk timesteps backward. At each (t, l), the cell backward
//   receives d_h_t[l] (sum of: d_h from t+1 backward + d_x from
//   layer l+1's backward at time t) and d_c_t[l] (from t+1 backward).
//   The cell returns (d_h_prev, d_x, d_c_prev); d_x feeds back into
//   d_h_t[l-1] (or d_xs[t] if l == 0) at the same timestep, and
//   d_h_prev/d_c_prev are saved as d_h_t/d_c_t for time t-1.

///|
/// Parameter bundle for a stack of L LSTM cells. All layers share
/// d_h; layer 0 has input dim `d_x`, subsequent layers have input
/// dim `d_h`.
pub struct StackedLstmParam {
  d_x : Int
  d_h : Int
  n_layers : Int
  layers : Array[LstmCellParam]
}

///|
/// Build a multi-layer LSTM with `n_layers` cells, each with hidden
/// dim `d_h`. Seeds are layered (seed, seed+1, ...).
pub fn StackedLstmParam::new(
  d_x : Int,
  d_h : Int,
  n_layers : Int,
  seed : UInt64,
) -> StackedLstmParam {
  let layers : Array[LstmCellParam] = []
  for l in 0..<n_layers {
    let layer_d_x = if l == 0 { d_x } else { d_h }
    let s = seed + (l * 7 + 1).to_uint64()
    layers.push(LstmCellParam::new(layer_d_x, d_h, s))
  }
  { d_x, d_h, n_layers, layers }
}

///|
/// Forward cache: per-timestep, per-layer cell caches plus the
/// forward outputs (needed by backward and for loss computation).
pub struct StackedLstmCache {
  caches : Array[Array[LstmCellCache]]
  hs : Array[Array[Array[Float]]]
  cs : Array[Array[Array[Float]]]
}

///|
/// Run the stacked LSTM forward over an input sequence. Returns
/// `(hs, cs, cache)` where `hs[t][l]` is the hidden state at time t
/// of layer l, and `cs[t][l]` is the cell state.
pub fn stacked_lstm_forward(
  xs : Array[Array[Float]],
  h0s : Array[Array[Float]],
  c0s : Array[Array[Float]],
  param : StackedLstmParam,
) -> (Array[Array[Array[Float]]], Array[Array[Array[Float]]], StackedLstmCache) {
  let n = xs.length()
  let l_count = param.n_layers
  let hs : Array[Array[Array[Float]]] = []
  let cs : Array[Array[Array[Float]]] = []
  let caches : Array[Array[LstmCellCache]] = []
  let mut cur_h : Array[Array[Float]] = h0s
  let mut cur_c : Array[Array[Float]] = c0s
  for t in 0..<n {
    let h_t : Array[Array[Float]] = []
    let c_t : Array[Array[Float]] = []
    let cache_t : Array[LstmCellCache] = []
    for l in 0..<l_count {
      let input_l = if l == 0 { xs[t] } else { h_t[l - 1] }
      let (h_l, cache_l) = lstm_cell_forward(
        input_l, cur_h[l], cur_c[l], param.layers[l],
      )
      h_t.push(h_l)
      c_t.push(cache_l.c_t)
      cache_t.push(cache_l)
    }
    hs.push(h_t)
    cs.push(c_t)
    caches.push(cache_t)
    cur_h = h_t
    cur_c = c_t
  }
  let cache : StackedLstmCache = { caches, hs, cs }
  (hs, cs, cache)
}

///|
/// Mean-squared-error loss on the top layer's hidden states vs
/// target sequence. Returns -1 on shape mismatch.
pub fn stacked_lstm_sequence_loss(
  hs : Array[Array[Array[Float]]],
  target : Array[Array[Float]],
) -> Float {
  let n = hs.length()
  if n == 0 {
    return 0.0F
  }
  if target.length() != n {
    return -1.0F
  }
  let mut sum = 0.0F
  let mut count = 0
  for t in 0..<n {
    let p = hs[t][hs[t].length() - 1]
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
/// BPTT backward for the stacked LSTM. Returns
/// `(d_xs, d_h0s, d_c0s, grads)`. The grads array has one
/// LstmCellGrad per layer.
pub fn stacked_lstm_backward(
  hs : Array[Array[Array[Float]]],
  target : Array[Array[Float]],
  cache : StackedLstmCache,
  _h0s : Array[Array[Float]],
  _c0s : Array[Array[Float]],
  param : StackedLstmParam,
) -> (Array[Array[Float]], Array[Array[Float]], Array[Array[Float]], Array[LstmCellGrad]) {
  let n = hs.length()
  let l_count = param.n_layers
  let d_h = param.d_h
  let d_x = param.d_x
  let count = n * d_h
  let scale = 2.0F / Float::from_int(count)
  // d_xs init.
  let d_xs : Array[Array[Float]] = Array::make(n, [])
  for t in 0..<n {
    d_xs[t] = Array::make(d_x, 0.0F)
  }
  // Per-layer grads init.
  let grads : Array[LstmCellGrad] = []
  for l in 0..<l_count {
    grads.push(LstmCellGrad::zero(param.layers[l]))
  }
  // d_h_layer[l], d_c_layer[l]: carryover from time t+1 (initial: zero).
  let d_h_layer : Array[Array[Float]] = []
  let d_c_layer : Array[Array[Float]] = []
  for l in 0..<l_count {
    d_h_layer.push(Array::make(d_h, 0.0F))
    d_c_layer.push(Array::make(d_h, 0.0F))
  }
  for t_step in 0..<n {
    let t = n - 1 - t_step
    let hs_t = hs[t]
    // Local copy of d_h at this timestep (so we can mutate without
    // affecting d_h_layer which we'll overwrite with d_h_prev below).
    let d_h_t : Array[Array[Float]] = []
    for l in 0..<l_count {
      d_h_t.push(zero_copy(d_h_layer[l]))
    }
    // Add loss gradient to the top layer.
    let top_l = l_count - 1
    for i in 0..<d_h {
      d_h_t[top_l][i] = d_h_t[top_l][i] + scale * (hs_t[top_l][i] - target[t][i])
    }
    // Process layers top-down so that d_x from layer l feeds into
    // d_h_t[l-1] BEFORE we run layer l-1's cell backward.
    for l_step in 0..<l_count {
      let l = l_count - 1 - l_step
      let (d_h_prev, d_x, d_c_prev) = lstm_cell_backward(
        cache.caches[t][l], d_h_t[l], d_c_layer[l], param.layers[l], grads[l],
      )
      // Save d_h_prev, d_c_prev as carryover for time t-1.
      d_h_layer[l] = d_h_prev
      d_c_layer[l] = d_c_prev
      if l == 0 {
        d_xs[t] = d_x
      } else {
        // Add d_x (length d_h) into d_h_t[l-1] for the next inner iter.
        for i in 0..<d_h {
          d_h_t[l - 1][i] = d_h_t[l - 1][i] + d_x[i]
        }
      }
    }
  }
  (d_xs, d_h_layer, d_c_layer, grads)
}

///|
/// Apply SGD to all layers using per-layer grads.
pub fn stacked_lstm_sgd_step(
  param : StackedLstmParam,
  grads : Array[LstmCellGrad],
  lr : Float,
) -> Unit {
  for l in 0..<param.n_layers {
    lstm_cell_sgd_step(param.layers[l], grads[l], lr)
  }
}

///|
/// Run K-step SGD training on a single input/target sequence.
/// Optionally clips grads to `[-clip, clip]` per layer.
pub fn stacked_lstm_train_n_steps(
  xs : Array[Array[Float]],
  target : Array[Array[Float]],
  h0s : Array[Array[Float]],
  c0s : Array[Array[Float]],
  param : StackedLstmParam,
  n_steps : Int,
  lr : Float,
  clip : Float,
) -> Float {
  let mut last_loss = 0.0F
  for _step in 0..<n_steps {
    let (hs, _cs, cache) = stacked_lstm_forward(xs, h0s, c0s, param)
    let loss = stacked_lstm_sequence_loss(hs, target)
    let (_d_xs, _d_h0s, _d_c0s, grads) = stacked_lstm_backward(
      hs, target, cache, h0s, c0s, param,
    )
    if clip > 0.0F {
      for l in 0..<param.n_layers {
        lstm_clip_grad(grads[l], clip)
      }
    }
    stacked_lstm_sgd_step(param, grads, lr)
    last_loss = loss
  }
  last_loss
}

///|
/// Build zero initial hidden/cell states for a stacked LSTM.
pub fn stacked_lstm_zero_init(
  n_layers : Int,
  d_h : Int,
) -> (Array[Array[Float]], Array[Array[Float]]) {
  let h0s : Array[Array[Float]] = []
  let c0s : Array[Array[Float]] = []
  for _ in 0..<n_layers {
    h0s.push(Array::make(d_h, 0.0F))
    c0s.push(Array::make(d_h, 0.0F))
  }
  (h0s, c0s)
}

///|
/// Generate a synthetic identity-shift task. xs has dim `d_x`;
/// target has dim `d_target`. For the simple demo, use d_x = d_h =
/// d_target so the loss is on a target matching the top layer's
/// hidden dim.
pub fn stacked_lstm_identity_dataset(
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
    let g : Array[Float] = Array::make(d_target, 0.0F)
    target[t] = g
  }
  // target[t] = xs[(t-1) mod n] in first min(d_x, d_target) entries.
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
