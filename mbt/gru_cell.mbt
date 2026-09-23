// gru_cell.mbt — Vanilla GRU cell forward + backward + SGD (v0.30.0).
//
// Single GRU cell step. Inputs at time t:
//   - x_t      : input, length d_x
//   - h_{t-1}  : hidden, length d_h
//
// Vanilla GRU (no reset-after-Fourier tricks, no layer-norm):
//   z_t = σ(W_z · [h_{t-1}; x_t] + b_z)         update gate
//   r_t = σ(W_r · [h_{t-1}; x_t] + b_r)         reset gate
//   s_t = r_t ⊙ h_{t-1}                        reset-applied hidden
//   n_t = tanh(W_n · [s_t; x_t] + b_n)         candidate
//   h_t = (1 - z_t) ⊙ h_{t-1} + z_t ⊙ n_t     hidden output
//
// This module provides:
//   - `GruCellParam`  — (w_z, w_r, w_n, b_z, b_r, b_n)
//   - `GruCellCache`  — intermediates for BPTT
//   - `GruCellGrad`   — parameter gradients (accumulated over time)
//   - `gru_cell_forward`  — single step forward
//   - `gru_cell_backward` — single step backward
//   - `gru_cell_sgd_step` — apply accumulated gradients (SGD)
//
// Like LSTM, struct fields MUST be lowercase (MoonBit requirement),
// so we use `w_z` instead of `W_z`. The mathematical notation `W_z`
// is preserved in the doc comments.

///|
/// GRU cell parameter bundle. Each `w_X` is d_h × (d_h + d_x); each
/// `b_X` has length d_h. W_n also has shape d_h × (d_h + d_x), but
/// its first d_h input slots see `r ⊙ h_prev` (not `h_prev`).
pub struct GruCellParam {
  d_x : Int
  d_h : Int
  w_z : Array[Array[Float]]
  w_r : Array[Array[Float]]
  w_n : Array[Array[Float]]
  b_z : Array[Float]
  b_r : Array[Float]
  b_n : Array[Float]
}

///|
/// Build a fresh GRU cell parameter with Xavier-normal init for the
/// three weight matrices and zero biases. seed controls RNG.
pub fn GruCellParam::new(
  d_x : Int,
  d_h : Int,
  seed : UInt64,
) -> GruCellParam {
  let in_dim = d_h + d_x
  let std = sqrtf(2.0F / Float::from_int(d_h + in_dim))
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let wz = xavier_normal(d_h, in_dim, std, rng)
  let wr = xavier_normal(d_h, in_dim, std, rng)
  let wn = xavier_normal(d_h, in_dim, std, rng)
  let bz : Array[Float] = Array::make(d_h, 0.0F)
  let br : Array[Float] = Array::make(d_h, 0.0F)
  let bn : Array[Float] = Array::make(d_h, 0.0F)
  { d_x, d_h, w_z: wz, w_r: wr, w_n: wn, b_z: bz, b_r: br, b_n: bn }
}

///|
/// Cache of intermediate values from a single GRU forward step.
pub struct GruCellCache {
  x : Array[Float]
  h_prev : Array[Float]
  z : Array[Float]
  r : Array[Float]
  s : Array[Float]
  n : Array[Float]
  h_t : Array[Float]
}

///|
/// Accumulated parameter gradients from a sequence of backward
/// steps. Initialise all entries to zero and accumulate via
/// `gru_cell_backward` + `gru_cell_sgd_step`.
pub struct GruCellGrad {
  d_w_z : Array[Array[Float]]
  d_w_r : Array[Array[Float]]
  d_w_n : Array[Array[Float]]
  d_b_z : Array[Float]
  d_b_r : Array[Float]
  d_b_n : Array[Float]
}

///|
/// Create a zero-initialised GruCellGrad matching the param shape.
pub fn GruCellGrad::zero(param : GruCellParam) -> GruCellGrad {
  let d_h = param.d_h
  let in_dim = param.d_h + param.d_x
  let zwz : Array[Array[Float]] = Array::make(d_h, [])
  let zwr : Array[Array[Float]] = Array::make(d_h, [])
  let zwn : Array[Array[Float]] = Array::make(d_h, [])
  for i in 0..<d_h {
    let row_z : Array[Float] = Array::make(in_dim, 0.0F)
    let row_r : Array[Float] = Array::make(in_dim, 0.0F)
    let row_n : Array[Float] = Array::make(in_dim, 0.0F)
    zwz[i] = row_z
    zwr[i] = row_r
    zwn[i] = row_n
  }
  let zb : Array[Float] = Array::make(d_h, 0.0F)
  {
    d_w_z: zwz,
    d_w_r: zwr,
    d_w_n: zwn,
    d_b_z: zero_copy(zb),
    d_b_r: zero_copy(zb),
    d_b_n: zero_copy(zb),
  }
}

///|
/// Forward step of a single GRU cell. Returns (h_t, cache).
pub fn gru_cell_forward(
  x : Array[Float],
  h_prev : Array[Float],
  param : GruCellParam,
) -> (Array[Float], GruCellCache) {
  let d_h = param.d_h
  let cat = vec_concat(h_prev, x)
  let z_pre = matvec(param.w_z, param.b_z, cat)
  let z = sigmoid_vec(z_pre)
  let r_pre = matvec(param.w_r, param.b_r, cat)
  let r = sigmoid_vec(r_pre)
  // s = r ⊙ h_prev
  let s : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    s[i] = r[i] * h_prev[i]
  }
  let cat_n = vec_concat(s, x)
  let n_pre = matvec(param.w_n, param.b_n, cat_n)
  let n = tanh_vec(n_pre)
  // h_t = (1 - z) ⊙ h_prev + z ⊙ n
  let h_t : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    h_t[i] = (1.0F - z[i]) * h_prev[i] + z[i] * n[i]
  }
  let cache : GruCellCache = { x, h_prev, z, r, s, n, h_t }
  (h_t, cache)
}

///|
/// Backward step of a single GRU cell. Returns (d_h_prev, d_x) —
/// gradients w.r.t. the cell's inputs. Parameter gradients
/// accumulate into `grad`.
pub fn gru_cell_backward(
  cache : GruCellCache,
  d_h_t : Array[Float],
  param : GruCellParam,
  grad : GruCellGrad,
) -> (Array[Float], Array[Float]) {
  let d_h = param.d_h
  let d_x = param.d_x
  let in_dim = d_h + d_x
  // 1. From h_t = (1-z) ⊙ h_prev + z ⊙ n:
  //    d_z  = d_h ⊙ (n - h_prev)
  //    d_n  = d_h ⊙ z
  //    d_h_prev_main = d_h ⊙ (1 - z)
  let d_z : Array[Float] = Array::make(d_h, 0.0F)
  let d_n : Array[Float] = Array::make(d_h, 0.0F)
  let d_h_prev_main : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    d_z[i] = d_h_t[i] * (cache.n[i] - cache.h_prev[i])
    d_n[i] = d_h_t[i] * cache.z[i]
    d_h_prev_main[i] = d_h_t[i] * (1.0F - cache.z[i])
  }
  // 2. d_pre_n = (1 - n²) ⊙ d_n
  let d_pre_n : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    d_pre_n[i] = (1.0F - cache.n[i] * cache.n[i]) * d_n[i]
  }
  // 3. cat_n = [s; x]. W_n is d_h × (d_h + d_x).
  //    d_s[j] = Σ_i W_n[i, j] · d_pre_n[i]   (j < d_h)
  //    d_x_from_n[j] = Σ_i W_n[i, d_h + j] · d_pre_n[i]  (j < d_x)
  let d_s : Array[Float] = Array::make(d_h, 0.0F)
  let d_x_from_n : Array[Float] = Array::make(d_x, 0.0F)
  for i in 0..<d_h {
    let row_n = param.w_n[i]
    for j in 0..<d_h {
      d_s[j] = d_s[j] + row_n[j] * d_pre_n[i]
    }
    for j in 0..<d_x {
      d_x_from_n[j] = d_x_from_n[j] + row_n[d_h + j] * d_pre_n[i]
    }
  }
  // 4. Accumulate W_n parameter gradients and bias.
  for i in 0..<d_h {
    grad.d_b_n[i] = grad.d_b_n[i] + d_pre_n[i]
    for j in 0..<in_dim {
      let cat_n_j = if j < d_h { cache.s[j] } else { cache.x[j - d_h] }
      grad.d_w_n[i][j] = grad.d_w_n[i][j] + d_pre_n[i] * cat_n_j
    }
  }
  // 5. s = r ⊙ h_prev:
  //    d_r = d_s ⊙ h_prev
  //    d_h_prev_s = d_s ⊙ r
  let d_r : Array[Float] = Array::make(d_h, 0.0F)
  let d_h_prev_s : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    d_r[i] = d_s[i] * cache.h_prev[i]
    d_h_prev_s[i] = d_s[i] * cache.r[i]
  }
  // 6. d_pre_z = d_z ⊙ z ⊙ (1 - z)
  //    d_pre_r = d_r ⊙ r ⊙ (1 - r)
  let d_pre_z : Array[Float] = Array::make(d_h, 0.0F)
  let d_pre_r : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    d_pre_z[i] = d_z[i] * cache.z[i] * (1.0F - cache.z[i])
    d_pre_r[i] = d_r[i] * cache.r[i] * (1.0F - cache.r[i])
  }
  // 7. d_pre_z and d_pre_r both go through cat = [h_prev; x].
  let cat = vec_concat(cache.h_prev, cache.x)
  let d_pre_z_full = matvec_backward_acc(
    param.w_z, grad.d_b_z, grad.d_w_z, cat, d_pre_z,
  )
  let d_pre_r_full = matvec_backward_acc(
    param.w_r, grad.d_b_r, grad.d_w_r, cat, d_pre_r,
  )
  // 8. Sum contributions.
  let d_h_prev : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    d_h_prev[i] = d_h_prev_main[i] + d_h_prev_s[i] + d_pre_z_full[i] + d_pre_r_full[i]
  }
  let d_x_out : Array[Float] = Array::make(d_x, 0.0F)
  for i in 0..<d_x {
    d_x_out[i] = d_x_from_n[i] + d_pre_z_full[d_h + i] + d_pre_r_full[d_h + i]
  }
  (d_h_prev, d_x_out)
}

///|
/// Apply accumulated gradients to parameters with simple SGD.
pub fn gru_cell_sgd_step(
  param : GruCellParam,
  grad : GruCellGrad,
  lr : Float,
) -> Unit {
  for i in 0..<param.d_h {
    let in_dim = param.w_z[i].length()
    for j in 0..<in_dim {
      param.w_z[i][j] = param.w_z[i][j] - lr * grad.d_w_z[i][j]
      param.w_r[i][j] = param.w_r[i][j] - lr * grad.d_w_r[i][j]
      param.w_n[i][j] = param.w_n[i][j] - lr * grad.d_w_n[i][j]
      grad.d_w_z[i][j] = 0.0F
      grad.d_w_r[i][j] = 0.0F
      grad.d_w_n[i][j] = 0.0F
    }
    param.b_z[i] = param.b_z[i] - lr * grad.d_b_z[i]
    param.b_r[i] = param.b_r[i] - lr * grad.d_b_r[i]
    param.b_n[i] = param.b_n[i] - lr * grad.d_b_n[i]
    grad.d_b_z[i] = 0.0F
    grad.d_b_r[i] = 0.0F
    grad.d_b_n[i] = 0.0F
  }
}
