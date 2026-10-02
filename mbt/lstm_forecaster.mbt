// lstm_forecaster.mbt — LSTM-based time-series forecaster with full
// BPTT + SGD training (v0.86.0).
//
// Architecture (univariate input, but input_dim is configurable for
// multivariate):
//
//   input_t (length input_dim)
//       ↓ Linear_in (input_dim → hidden_dim)
//   x_proj_t (length hidden_dim)
//       ↓ LstmCell (over time)
//   h_t (length hidden_dim)
//       ↓ Linear_out (hidden_dim → output_dim)
//   y_hat_t (length output_dim)            — point forecast at step t+1
//
// Loss: MSE between y_hat_t and target_{t+1} (next-step prediction).
// Training: full BPTT through the LSTM cell (using
// `lstm_cell_backward` from lstm_cell.mbt) plus analytic gradients
// through the input/output Linear projections. Single SGD step on
// all weights per `train_step`.
//
// Reference: Hochreiter & Schmidhuber 1997; identical to the
// input/output pattern of the v0.61.0 LSTM actor but for the
// forecasting use case (scalar regression head instead of tanh
// action squash).

///|
/// LSTM time-series forecaster. Projects input → hidden via Linear_in,
// runs an LstmCell over time, projects hidden → output via Linear_out.
pub struct LSTMForecaster {
  input_dim : Int
  hidden_dim : Int
  output_dim : Int
  // Linear_in: (hidden_dim × input_dim) + bias of length hidden_dim
  in_w : Array[Array[Float]]
  in_b : Array[Float]
  // LSTM cell
  lstm : LstmCellParam
  // Linear_out: (output_dim × hidden_dim) + bias of length output_dim
  out_w : Array[Array[Float]]
  out_b : Array[Float]
}

///|
/// Build fresh LSTMForecaster. Weights init via xavier_normal scaled
/// by sqrt(2/fan_in) (He-style for ReLU-style activation; here we use
/// tanh internally via the LSTM cell, so this matches the standard).
pub fn LSTMForecaster::new(
  input_dim : Int,
  hidden_dim : Int,
  output_dim : Int,
  seed : UInt64,
) -> LSTMForecaster {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std_in = sqrtf(2.0F / Float::from_int(input_dim))
  let in_w = xavier_normal(hidden_dim, input_dim, std_in, rng1)
  let in_b : Array[Float] = Array::make(hidden_dim, 0.0F)
  let lstm = LstmCellParam::new(hidden_dim, hidden_dim, seed + 4UL)
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
    lstm,
    out_w,
    out_b,
  }
}

///|
/// Per-step forward. Returns `(y_hat, h_t, c_t, cache)`.
/// `y_hat` is the forecast vector (length output_dim) for the NEXT
/// step given the current input. `cache` is the LstmCellCache for
/// BPTT backward.
fn lstm_forecaster_step_with_cache(
  model : LSTMForecaster,
  input_t : Array[Float],
  h_prev : Array[Float],
  c_prev : Array[Float],
) -> (Array[Float], Array[Float], Array[Float], LstmCellCache) {
  // x_proj = in_w · input + in_b
  let x_proj = matvec(model.in_w, model.in_b, input_t)
  // h_t, cache = LSTM_cell(x_proj, h_prev, c_prev)
  let (h_t, cache) = lstm_cell_forward(x_proj, h_prev, c_prev, model.lstm)
  let c_t = cache.c_t
  // y_hat = out_w · h_t + out_b
  let y_hat = matvec(model.out_w, model.out_b, h_t)
  (y_hat, h_t, c_t, cache)
}

///|
/// T-step sequence forward. `input_seq` flat row-major
/// `[seq_len × input_dim]`. Returns `(y_hat_seq, h_final, c_final)`
/// where `y_hat_seq` is flat `[seq_len × output_dim]`.
/// Each step's prediction is the "next step" forecast given the
/// current input — caller typically pairs `y_hat_seq[t]` with
/// `target_seq[t+1]` for training.
pub fn lstm_forecaster_seq_forward(
  model : LSTMForecaster,
  input_seq : Array[Float],
  seq_len : Int,
  h_init : Array[Float],
  c_init : Array[Float],
) -> (Array[Float], Array[Float], Array[Float]) {
  let y_hat_seq : Array[Float] = Array::make(
    seq_len * model.output_dim, 0.0F,
  )
  let mut h = h_init
  let mut c = c_init
  for t in 0..<seq_len {
    let off = t * model.input_dim
    let input_t : Array[Float] = Array::make(model.input_dim, 0.0F)
    for k in 0..<model.input_dim {
      input_t[k] = input_seq[off + k]
    }
    let (y_hat, new_h, new_c, _cache) = lstm_forecaster_step_with_cache(
      model, input_t, h, c,
    )
    let y_off = t * model.output_dim
    for k in 0..<model.output_dim {
      y_hat_seq[y_off + k] = y_hat[k]
    }
    h = new_h
    c = new_c
  }
  (y_hat_seq, h, c)
}

///|
/// Compute MSE loss between predicted and target sequences.
/// Both flat `[seq_len × output_dim]`.
pub fn lstm_forecaster_mse_loss(
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
fn lstm_forecaster_mse_grad(
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
/// One full-BPTT SGD training step on the LSTMForecaster. Computes the
// MSE loss + analytic gradients through Linear_out, BPTT through the
/// LSTM cell via `lstm_cell_backward`, and analytic gradients through
/// Linear_in. Returns `(updated_model, loss)`.
pub fn lstm_forecaster_train_step(
  model : LSTMForecaster,
  input_seq : Array[Float],
  target_seq : Array[Float],
  seq_len : Int,
  h_init : Array[Float],
  c_init : Array[Float],
  lr : Float,
) -> (LSTMForecaster, Float) {
  let in_dim = model.input_dim
  let h_dim = model.hidden_dim
  let out_dim = model.output_dim
  // 1. Forward + per-step caches.
  let y_hat_seq : Array[Float] = Array::make(
    seq_len * out_dim, 0.0F,
  )
  let x_proj_seq : Array[Float] = Array::make(seq_len * h_dim, 0.0F)
  let h_seq : Array[Float] = Array::make(seq_len * h_dim, 0.0F)
  let mut h = h_init
  let mut c = c_init
  let lstm_cache_seq : Array[LstmCellCache] = Array::make(seq_len, {
    x: Array::make(h_dim, 0.0F),
    h_prev: Array::make(h_dim, 0.0F),
    c_prev: Array::make(h_dim, 0.0F),
    f: Array::make(h_dim, 0.0F),
    ig: Array::make(h_dim, 0.0F),
    c_tilde: Array::make(h_dim, 0.0F),
    o: Array::make(h_dim, 0.0F),
    c_t: Array::make(h_dim, 0.0F),
    tanh_c_t: Array::make(h_dim, 0.0F),
    h_t: Array::make(h_dim, 0.0F),
  })
  let c_seq : Array[Float] = Array::make(seq_len * h_dim, 0.0F)
  for t in 0..<seq_len {
    let off = t * in_dim
    let input_t : Array[Float] = Array::make(in_dim, 0.0F)
    for k in 0..<in_dim {
      input_t[k] = input_seq[off + k]
    }
    let (y_hat, new_h, new_c, cache) = lstm_forecaster_step_with_cache(
      model, input_t, h, c,
    )
    let y_off = t * out_dim
    let xp_off = t * h_dim
    for k in 0..<out_dim {
      y_hat_seq[y_off + k] = y_hat[k]
    }
    for k in 0..<h_dim {
      x_proj_seq[xp_off + k] = cache.x[k]
      h_seq[xp_off + k] = new_h[k]
      c_seq[xp_off + k] = new_c[k]
    }
    lstm_cache_seq[t] = cache
    h = new_h
    c = new_c
  }
  // 2. Loss.
  let loss = lstm_forecaster_mse_loss(y_hat_seq, target_seq, seq_len, out_dim)
  // 3. Gradient w.r.t. y_hat outputs.
  let grad_y_hat = lstm_forecaster_mse_grad(y_hat_seq, target_seq, seq_len, out_dim)
  // 4. Gradient w.r.t. out_w, out_b.
  //    d_y_hat[t, i] -> d_out_w[i, k] += d_y_hat[t, i] · h_seq[t, k]
  //                    d_out_b[i]    += d_y_hat[t, i]
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
  //    d_h_seq[t, k] = Σ_i grad_y_hat[t, i] · out_w[i, k]
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
  // 6. BPTT through the LSTM cell, in reverse time.
  let mut d_h = Array::make(h_dim, 0.0F)  // d_h_t
  let mut d_c = Array::make(h_dim, 0.0F)  // d_c_t
  let d_in_w : Array[Array[Float]] = Array::make(
    h_dim, Array::make(in_dim, 0.0F),
  )
  let d_in_b : Array[Float] = Array::make(h_dim, 0.0F)
  let lstm_grad = LstmCellGrad::zero(model.lstm)
  for t_rev in 0..<seq_len {
    let t = seq_len - 1 - t_rev
    let h_off = t * h_dim
    // Combine upstream d_h from out_w + BPTT carry.
    let d_h_combined : Array[Float] = Array::make(h_dim, 0.0F)
    for k in 0..<h_dim {
      d_h_combined[k] = d_h_seq[h_off + k] + d_h[k]
    }
    let cache = lstm_cache_seq[t]
    let (d_h_prev, d_x, d_c_prev) = lstm_cell_backward(
      cache, d_h_combined, d_c, model.lstm, lstm_grad,
    )
    // d_x is gradient w.r.t. x_proj (post-Linear_in). Backprop through
    // Linear_in: d_in_w[i, k] += d_x[i] · input_seq[t, k]
    //           d_in_b[i]    += d_x[i]
    let in_off = t * in_dim
    for i in 0..<h_dim {
      let g = d_x[i]
      d_in_b[i] = d_in_b[i] + g
      for k in 0..<in_dim {
        d_in_w[i][k] = d_in_w[i][k] + g * input_seq[in_off + k]
      }
    }
    d_h = d_h_prev
    d_c = d_c_prev
  }
  // 7. SGD step on all parameter groups.
  // Linear_out
  let (new_out_w, new_out_b) = sgd_update_arrays(
    flatten_2d(model.out_w), model.out_b,
    flatten_2d(d_out_w), d_out_b,
    lr,
  )
  // Linear_in
  let (new_in_w, new_in_b) = sgd_update_arrays(
    flatten_2d(model.in_w), model.in_b,
    flatten_2d(d_in_w), d_in_b,
    lr,
  )
  // LSTM cell — flatten the 4 gate matrices and biases.
  let (new_w_f, new_b_f) = sgd_update_arrays(
    flatten_2d(model.lstm.w_f), model.lstm.b_f,
    flatten_2d(lstm_grad.d_w_f), lstm_grad.d_b_f,
    lr,
  )
  let (new_w_i, new_b_i) = sgd_update_arrays(
    flatten_2d(model.lstm.w_i), model.lstm.b_i,
    flatten_2d(lstm_grad.d_w_i), lstm_grad.d_b_i,
    lr,
  )
  let (new_w_c, new_b_c) = sgd_update_arrays(
    flatten_2d(model.lstm.w_c), model.lstm.b_c,
    flatten_2d(lstm_grad.d_w_c), lstm_grad.d_b_c,
    lr,
  )
  let (new_w_o, new_b_o) = sgd_update_arrays(
    flatten_2d(model.lstm.w_o), model.lstm.b_o,
    flatten_2d(lstm_grad.d_w_o), lstm_grad.d_b_o,
    lr,
  )
  let new_lstm : LstmCellParam = {
    d_h: model.lstm.d_h,
    d_x: model.lstm.d_x,
    w_f: unflatten_2d(new_w_f, h_dim, h_dim + in_dim),
    b_f: new_b_f,
    w_i: unflatten_2d(new_w_i, h_dim, h_dim + in_dim),
    b_i: new_b_i,
    w_c: unflatten_2d(new_w_c, h_dim, h_dim + in_dim),
    b_c: new_b_c,
    w_o: unflatten_2d(new_w_o, h_dim, h_dim + in_dim),
    b_o: new_b_o,
  }
  let updated : LSTMForecaster = {
    ..model,
    in_w: unflatten_2d(new_in_w, h_dim, in_dim),
    in_b: new_in_b,
    lstm: new_lstm,
    out_w: unflatten_2d(new_out_w, out_dim, h_dim),
    out_b: new_out_b,
  }
  let _ = c_seq  // currently unused (reserved for future h-cell gradient)
  let _ = x_proj_seq
  (updated, loss)
}