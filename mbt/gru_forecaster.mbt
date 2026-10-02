// gru_forecaster.mbt — GRU-based time-series forecaster with full BPTT
// + SGD training (v0.87.0).
//
// Architecture (parallel to v0.86.0 LSTMForecaster but with GRU cell):
//
//   input_t (length input_dim)
//       ↓ Linear_in (input_dim → hidden_dim)
//   x_proj_t (length hidden_dim)
//       ↓ GruCell (over time)
//   h_t (length hidden_dim)
//       ↓ Linear_out (hidden_dim → output_dim)
//   y_hat_t (length output_dim)            — point forecast at step t+1
//
// Loss: MSE between y_hat_t and target_{t+1}. Training: full BPTT
// through the GRU cell via `gru_cell_backward` plus analytic gradients
// through Linear_in / Linear_out. Single SGD step on all weights per
// `train_step`.
//
// Reference: Cho et al. 2014 "Learning Phrase Representations using
// RNN Encoder-Decoder for Statistical Machine Translation".

///|
/// GRU time-series forecaster. Projects input → hidden via Linear_in,
/// runs a GruCell over time, projects hidden → output via Linear_out.
pub struct GRUForecaster {
  input_dim : Int
  hidden_dim : Int
  output_dim : Int
  // Linear_in: (hidden_dim × input_dim) + bias of length hidden_dim
  in_w : Array[Array[Float]]
  in_b : Array[Float]
  // GRU cell
  gru : GruCellParam
  // Linear_out: (output_dim × hidden_dim) + bias of length output_dim
  out_w : Array[Array[Float]]
  out_b : Array[Float]
}

///|
/// Build fresh GRUForecaster. Weights init via xavier_normal scaled
/// by sqrt(2/fan_in) (matches v0.86.0 LSTMForecaster init style).
pub fn GRUForecaster::new(
  input_dim : Int,
  hidden_dim : Int,
  output_dim : Int,
  seed : UInt64,
) -> GRUForecaster {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std_in = sqrtf(2.0F / Float::from_int(input_dim))
  let in_w = xavier_normal(hidden_dim, input_dim, std_in, rng1)
  let in_b : Array[Float] = Array::make(hidden_dim, 0.0F)
  let gru = GruCellParam::new(input_dim, hidden_dim, seed + 4UL)
  let rng2 = Xoshiro::from_state(
    seed + 8UL, seed + 9UL, seed + 10UL, seed + 11UL,
  )
  let std_out = sqrtf(2.0F / Float::from_int(hidden_dim))
  let out_w = xavier_normal(output_dim, hidden_dim, std_out, rng2)
  let out_b : Array[Float] = Array::make(output_dim, 0.0F)
  {
    input_dim,
    hidden_dim,
    output_dim,
    in_w,
    in_b,
    gru,
    out_w,
    out_b,
  }
}

///|
/// Per-step forward. Returns `(y_hat, h_t, cache)`. The GRU cell has
/// only a hidden state (no separate cell state).
fn gru_forecaster_step_with_cache(
  model : GRUForecaster,
  input_t : Array[Float],
  h_prev : Array[Float],
) -> (Array[Float], Array[Float], GruCellCache) {
  // x_proj = in_w · input + in_b
  let x_proj = matvec(model.in_w, model.in_b, input_t)
  // h_t, cache = GRU_cell(x_proj, h_prev)
  let (h_t, cache) = gru_cell_forward(x_proj, h_prev, model.gru)
  // y_hat = out_w · h_t + out_b
  let y_hat = matvec(model.out_w, model.out_b, h_t)
  (y_hat, h_t, cache)
}

///|
/// T-step sequence forward. `input_seq` flat row-major
/// `[seq_len × input_dim]`. Returns `(y_hat_seq, h_final)` where
/// `y_hat_seq` is flat `[seq_len × output_dim]`.
pub fn gru_forecaster_seq_forward(
  model : GRUForecaster,
  input_seq : Array[Float],
  seq_len : Int,
  h_init : Array[Float],
) -> (Array[Float], Array[Float]) {
  let y_hat_seq : Array[Float] = Array::make(
    seq_len * model.output_dim, 0.0F,
  )
  let mut h = h_init
  for t in 0..<seq_len {
    let off = t * model.input_dim
    let input_t : Array[Float] = Array::make(model.input_dim, 0.0F)
    for k in 0..<model.input_dim {
      input_t[k] = input_seq[off + k]
    }
    let (y_hat, new_h, _cache) = gru_forecaster_step_with_cache(
      model, input_t, h,
    )
    let y_off = t * model.output_dim
    for k in 0..<model.output_dim {
      y_hat_seq[y_off + k] = y_hat[k]
    }
    h = new_h
  }
  (y_hat_seq, h)
}

///|
/// Compute MSE loss between predicted and target sequences.
/// Both flat `[seq_len × output_dim]`.
pub fn gru_forecaster_mse_loss(
  y_hat_seq : Array[Float],
  target_seq : Array[Float],
  seq_len : Int,
  output_dim : Int,
) -> Float {
  let n = seq_len * output_dim
  if n <= 0 {
    return 0.0F
  }
  let mut sum_sq = 0.0F
  for i in 0..<n {
    let d = y_hat_seq[i] - target_seq[i]
    sum_sq = sum_sq + d * d
  }
  sum_sq / Float::from_int(n)
}

///|
/// Per-step gradient of MSE w.r.t. y_hat: (2 / N) · (y_hat - target).
fn gru_forecaster_mse_grad(
  y_hat_seq : Array[Float],
  target_seq : Array[Float],
  seq_len : Int,
  output_dim : Int,
) -> Array[Float] {
  let n = seq_len * output_dim
  let grad : Array[Float] = Array::make(n, 0.0F)
  if n <= 0 {
    return grad
  }
  let scale = 2.0F / Float::from_int(n)
  for i in 0..<n {
    grad[i] = scale * (y_hat_seq[i] - target_seq[i])
  }
  grad
}

///|
/// One full-BPTT SGD training step on the GRUForecaster. Returns
/// `(updated_model, loss)`.
pub fn gru_forecaster_train_step(
  model : GRUForecaster,
  input_seq : Array[Float],
  target_seq : Array[Float],
  seq_len : Int,
  h_init : Array[Float],
  lr : Float,
) -> (GRUForecaster, Float) {
  let in_dim = model.input_dim
  let h_dim = model.hidden_dim
  let out_dim = model.output_dim
  // 1. Forward + per-step caches.
  let y_hat_seq : Array[Float] = Array::make(
    seq_len * out_dim, 0.0F,
  )
  let h_seq : Array[Float] = Array::make(seq_len * h_dim, 0.0F)
  let mut h = h_init
  let gru_cache_seq : Array[GruCellCache] = Array::make(seq_len, {
    x: Array::make(in_dim, 0.0F),
    h_prev: Array::make(h_dim, 0.0F),
    z: Array::make(h_dim, 0.0F),
    r: Array::make(h_dim, 0.0F),
    s: Array::make(h_dim, 0.0F),
    n: Array::make(h_dim, 0.0F),
    h_t: Array::make(h_dim, 0.0F),
  })
  for t in 0..<seq_len {
    let off = t * in_dim
    let input_t : Array[Float] = Array::make(in_dim, 0.0F)
    for k in 0..<in_dim {
      input_t[k] = input_seq[off + k]
    }
    let (y_hat, new_h, cache) = gru_forecaster_step_with_cache(
      model, input_t, h,
    )
    let y_off = t * out_dim
    let h_off = t * h_dim
    for k in 0..<out_dim {
      y_hat_seq[y_off + k] = y_hat[k]
    }
    for k in 0..<h_dim {
      h_seq[h_off + k] = new_h[k]
    }
    gru_cache_seq[t] = cache
    h = new_h
  }
  // 2. Loss.
  let loss = gru_forecaster_mse_loss(y_hat_seq, target_seq, seq_len, out_dim)
  // 3. Gradient w.r.t. y_hat outputs.
  let grad_y_hat = gru_forecaster_mse_grad(y_hat_seq, target_seq, seq_len, out_dim)
  // 4. Gradient w.r.t. out_w, out_b.
  let d_out_w : Array[Array[Float]] = Array::make(
    out_dim, Array::make(h_dim, 0.0F),
  )
  let d_out_b : Array[Float] = Array::make(out_dim, 0.0F)
  for t in 0..<seq_len {
    let y_off = t * out_dim
    let h_off = t * h_dim
    for i in 0..<out_dim {
      let g = grad_y_hat[y_off + i]
      d_out_b[i] = d_out_b[i] + g
      for k in 0..<h_dim {
        d_out_w[i][k] = d_out_w[i][k] + g * h_seq[h_off + k]
      }
    }
  }
  // 5. Gradient w.r.t. h_seq from out_w.
  let d_h_seq : Array[Float] = Array::make(seq_len * h_dim, 0.0F)
  for t in 0..<seq_len {
    let y_off = t * out_dim
    let h_off = t * h_dim
    for k in 0..<h_dim {
      let mut s = 0.0F
      for i in 0..<out_dim {
        s = s + grad_y_hat[y_off + i] * model.out_w[i][k]
      }
      d_h_seq[h_off + k] = s
    }
  }
  // 6. BPTT through the GRU cell, in reverse time.
  let mut d_h = Array::make(h_dim, 0.0F)
  let d_in_w : Array[Array[Float]] = Array::make(
    h_dim, Array::make(in_dim, 0.0F),
  )
  let d_in_b : Array[Float] = Array::make(h_dim, 0.0F)
  let gru_grad = GruCellGrad::zero(model.gru)
  for t_rev in 0..<seq_len {
    let t = seq_len - 1 - t_rev
    let h_off = t * h_dim
    let d_h_combined : Array[Float] = Array::make(h_dim, 0.0F)
    for k in 0..<h_dim {
      d_h_combined[k] = d_h_seq[h_off + k] + d_h[k]
    }
    let cache = gru_cache_seq[t]
    let (d_h_prev, d_x) = gru_cell_backward(
      cache, d_h_combined, model.gru, gru_grad,
    )
    let in_off = t * in_dim
    for i in 0..<h_dim {
      let g = d_x[i]
      d_in_b[i] = d_in_b[i] + g
      for k in 0..<in_dim {
        d_in_w[i][k] = d_in_w[i][k] + g * input_seq[in_off + k]
      }
    }
    d_h = d_h_prev
  }
  // 7. SGD step on all parameter groups.
  let (new_out_w, new_out_b) = sgd_update_arrays(
    flatten_2d(model.out_w), model.out_b,
    flatten_2d(d_out_w), d_out_b,
    lr,
  )
  let (new_in_w, new_in_b) = sgd_update_arrays(
    flatten_2d(model.in_w), model.in_b,
    flatten_2d(d_in_w), d_in_b,
    lr,
  )
  let (new_w_z, new_b_z) = sgd_update_arrays(
    flatten_2d(model.gru.w_z), model.gru.b_z,
    flatten_2d(gru_grad.d_w_z), gru_grad.d_b_z,
    lr,
  )
  let (new_w_r, new_b_r) = sgd_update_arrays(
    flatten_2d(model.gru.w_r), model.gru.b_r,
    flatten_2d(gru_grad.d_w_r), gru_grad.d_b_r,
    lr,
  )
  let (new_w_n, new_b_n) = sgd_update_arrays(
    flatten_2d(model.gru.w_n), model.gru.b_n,
    flatten_2d(gru_grad.d_w_n), gru_grad.d_b_n,
    lr,
  )
  let in_dim_total = h_dim + in_dim
  let new_gru : GruCellParam = {
    d_x: in_dim,
    d_h: h_dim,
    w_z: unflatten_2d(new_w_z, h_dim, in_dim_total),
    b_z: new_b_z,
    w_r: unflatten_2d(new_w_r, h_dim, in_dim_total),
    b_r: new_b_r,
    w_n: unflatten_2d(new_w_n, h_dim, in_dim_total),
    b_n: new_b_n,
  }
  let updated : GRUForecaster = {
    ..model,
    in_w: unflatten_2d(new_in_w, h_dim, in_dim),
    in_b: new_in_b,
    gru: new_gru,
    out_w: unflatten_2d(new_out_w, out_dim, h_dim),
    out_b: new_out_b,
  }
  (updated, loss)
}