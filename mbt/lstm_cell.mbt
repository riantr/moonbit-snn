// lstm_cell.mbt — Vanilla LSTM cell forward + backward (v0.29.0).
//
// Single LSTM cell step. Inputs at time t:
//   - x_t   : input,  length d_x
//   - h_{t-1} : hidden, length d_h
//   - c_{t-1} : cell state, length d_h
//
// Vanilla LSTM (no peepholes):
//   f_t = σ(W_f · [h_{t-1}; x_t] + b_f)         forget gate
//   i_t = σ(W_i · [h_{t-1}; x_t] + b_i)         input gate
//   C̃_t = tanh(W_C · [h_{t-1}; x_t] + b_C)     candidate cell
//   C_t = f_t ⊙ C_{t-1} + i_t ⊙ C̃_t            cell state
//   o_t = σ(W_o · [h_{t-1}; x_t] + b_o)         output gate
//   h_t = o_t ⊙ tanh(C_t)                      hidden output
//
// This module provides:
//   - `LstmCellParam`  — (w_f, w_i, w_c, w_o, b_f, b_i, b_c, b_o)
//   - `LstmCellCache`  — intermediates for BPTT
//   - `LstmCellGrad`   — parameter gradients (accumulated over time)
//   - `lstm_cell_forward`  — single step forward
//   - `lstm_cell_backward` — single step backward
//   - `lstm_cell_sgd_step` — apply accumulated gradients (SGD)
//
// Note: struct fields MUST be lowercase (MoonBit requirement), so we
// use `w_f` instead of `W_f`. The mathematical notation `W_f` is
// preserved in the doc comments.

///|
/// LSTM cell parameter bundle. Each `w_X` is d_h × (d_h + d_x); each
/// `b_X` has length d_h.
pub struct LstmCellParam {
  d_x : Int
  d_h : Int
  w_f : Array[Array[Float]]
  w_i : Array[Array[Float]]
  w_c : Array[Array[Float]]
  w_o : Array[Array[Float]]
  b_f : Array[Float]
  b_i : Array[Float]
  b_c : Array[Float]
  b_o : Array[Float]
}

///|
/// Build a fresh LSTM cell parameter with Xavier-normal init for
/// the four weight matrices and zero biases. seed controls RNG.
pub fn LstmCellParam::new(
  d_x : Int,
  d_h : Int,
  seed : UInt64,
) -> LstmCellParam {
  let in_dim = d_h + d_x
  let std = sqrtf(2.0F / Float::from_int(d_h + in_dim))
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let wf = xavier_normal(d_h, in_dim, std, rng)
  let wi = xavier_normal(d_h, in_dim, std, rng)
  let wc = xavier_normal(d_h, in_dim, std, rng)
  let wo = xavier_normal(d_h, in_dim, std, rng)
  let bf : Array[Float] = Array::make(d_h, 0.0F)
  let bi : Array[Float] = Array::make(d_h, 0.0F)
  let bc : Array[Float] = Array::make(d_h, 0.0F)
  let bo : Array[Float] = Array::make(d_h, 0.0F)
  { d_x, d_h, w_f: wf, w_i: wi, w_c: wc, w_o: wo, b_f: bf, b_i: bi, b_c: bc, b_o: bo }
}

///|
/// Cache of intermediate values from a single LSTM forward step.
pub struct LstmCellCache {
  x : Array[Float]
  h_prev : Array[Float]
  c_prev : Array[Float]
  f : Array[Float]
  ig : Array[Float]
  c_tilde : Array[Float]
  o : Array[Float]
  c_t : Array[Float]
  tanh_c_t : Array[Float]
  h_t : Array[Float]
}

///|
/// Accumulated parameter gradients from a sequence of backward
/// steps. Initialise all entries to zero and accumulate via
/// `lstm_cell_backward` + `lstm_cell_sgd_step`.
pub struct LstmCellGrad {
  d_w_f : Array[Array[Float]]
  d_w_i : Array[Array[Float]]
  d_w_c : Array[Array[Float]]
  d_w_o : Array[Array[Float]]
  d_b_f : Array[Float]
  d_b_i : Array[Float]
  d_b_c : Array[Float]
  d_b_o : Array[Float]
}

///|
/// Create a zero-initialised LstmCellGrad matching the param shape.
pub fn LstmCellGrad::zero(param : LstmCellParam) -> LstmCellGrad {
  let d_h = param.d_h
  let in_dim = param.d_h + param.d_x
  let zwf : Array[Array[Float]] = Array::make(d_h, [])
  let zwi : Array[Array[Float]] = Array::make(d_h, [])
  let zwc : Array[Array[Float]] = Array::make(d_h, [])
  let zwo : Array[Array[Float]] = Array::make(d_h, [])
  for i in 0..<d_h {
    let row_f : Array[Float] = Array::make(in_dim, 0.0F)
    let row_i : Array[Float] = Array::make(in_dim, 0.0F)
    let row_c : Array[Float] = Array::make(in_dim, 0.0F)
    let row_o : Array[Float] = Array::make(in_dim, 0.0F)
    zwf[i] = row_f
    zwi[i] = row_i
    zwc[i] = row_c
    zwo[i] = row_o
  }
  let zb : Array[Float] = Array::make(d_h, 0.0F)
  {
    d_w_f: zwf,
    d_w_i: zwi,
    d_w_c: zwc,
    d_w_o: zwo,
    d_b_f: zero_copy(zb),
    d_b_i: zero_copy(zb),
    d_b_c: zero_copy(zb),
    d_b_o: zero_copy(zb),
  }
}

fn zero_copy(a : Array[Float]) -> Array[Float] {
  let n = a.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = a[i]
  }
  out
}

fn xavier_normal(
  rows : Int,
  cols : Int,
  std : Float,
  rng : Xoshiro,
) -> Array[Array[Float]] {
  let w : Array[Array[Float]] = Array::make(rows, [])
  for i in 0..<rows {
    let row : Array[Float] = Array::make(cols, 0.0F)
    for j in 0..<cols {
      let (z1, z2) = box_muller(rng)
      let z = if z1 != 0.0 { z1 } else { z2 }
      row[j] = Float::from_double(z) * std
    }
    w[i] = row
  }
  w
}

fn sigmoid_vec(x : Array[Float]) -> Array[Float] {
  let n = x.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = sigmoid_f32(x[i])
  }
  out
}

fn tanh_vec(x : Array[Float]) -> Array[Float] {
  let n = x.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = tanhf(x[i])
  }
  out
}

///|
/// Matrix-vector product `y = W · x + b`. Local helper to avoid
/// collision with `linear_forward` in `linear.mbt`.
fn matvec(
  w : Array[Array[Float]],
  b : Array[Float],
  x : Array[Float],
) -> Array[Float] {
  let d_out = w.length()
  let y : Array[Float] = Array::make(d_out, 0.0F)
  for i in 0..<d_out {
    let mut s = b[i]
    let row = w[i]
    for j in 0..<row.length() {
      s = s + row[j] * x[j]
    }
    y[i] = s
  }
  y
}

///|
/// Backward through `matvec` accumulating parameter gradients.
/// Given d_y, computes d_x = Wᵀ · d_y and updates d_b += d_y,
/// d_W += d_y ⊗ x. Returns d_x.
fn matvec_backward_acc(
  w : Array[Array[Float]],
  d_b_acc : Array[Float],
  d_w_acc : Array[Array[Float]],
  x : Array[Float],
  d_y : Array[Float],
) -> Array[Float] {
  let d_out = w.length()
  let d_in = x.length()
  let d_x : Array[Float] = Array::make(d_in, 0.0F)
  for i in 0..<d_out {
    d_b_acc[i] = d_b_acc[i] + d_y[i]
  }
  for i in 0..<d_out {
    let row = w[i]
    let w_acc_row = d_w_acc[i]
    for j in 0..<d_in {
      w_acc_row[j] = w_acc_row[j] + d_y[i] * x[j]
      d_x[j] = d_x[j] + row[j] * d_y[i]
    }
  }
  d_x
}

///|
/// Concatenate two vectors [a; b].
fn vec_concat(
  a : Array[Float],
  b : Array[Float],
) -> Array[Float] {
  let out : Array[Float] = Array::make(a.length() + b.length(), 0.0F)
  for i in 0..<a.length() {
    out[i] = a[i]
  }
  for i in 0..<b.length() {
    out[a.length() + i] = b[i]
  }
  out
}

///|
/// Split a vector by length. Returns (first n_first, rest).
fn vec_split(
  v : Array[Float],
  n_first : Int,
) -> (Array[Float], Array[Float]) {
  let first : Array[Float] = Array::make(n_first, 0.0F)
  let second : Array[Float] = Array::make(v.length() - n_first, 0.0F)
  for i in 0..<n_first {
    first[i] = v[i]
  }
  for i in 0..<(v.length() - n_first) {
    second[i] = v[n_first + i]
  }
  (first, second)
}

///|
/// Forward step of a single LSTM cell. Returns (h_t, cache).
pub fn lstm_cell_forward(
  x : Array[Float],
  h_prev : Array[Float],
  c_prev : Array[Float],
  param : LstmCellParam,
) -> (Array[Float], LstmCellCache) {
  let d_h = param.d_h
  let cat = vec_concat(h_prev, x)
  let f_pre = matvec(param.w_f, param.b_f, cat)
  let f = sigmoid_vec(f_pre)
  let i_pre = matvec(param.w_i, param.b_i, cat)
  let ig = sigmoid_vec(i_pre)
  let c_pre = matvec(param.w_c, param.b_c, cat)
  let c_tilde = tanh_vec(c_pre)
  let o_pre = matvec(param.w_o, param.b_o, cat)
  let o = sigmoid_vec(o_pre)
  // Cell state.
  let c_t : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    c_t[i] = f[i] * c_prev[i] + ig[i] * c_tilde[i]
  }
  let tanh_c_t = tanh_vec(c_t)
  let h_t : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    h_t[i] = o[i] * tanh_c_t[i]
  }
  let cache : LstmCellCache = {
    x,
    h_prev,
    c_prev,
    f,
    ig,
    c_tilde,
    o,
    c_t,
    tanh_c_t,
    h_t,
  }
  (h_t, cache)
}

///|
/// Backward step of a single LSTM cell. Returns
/// (d_h_prev, d_x, d_c_prev) — gradients w.r.t. the cell's inputs.
/// Parameter gradients accumulate into `grad`.
pub fn lstm_cell_backward(
  cache : LstmCellCache,
  d_h_t : Array[Float],
  d_c_t : Array[Float],
  param : LstmCellParam,
  grad : LstmCellGrad,
) -> (Array[Float], Array[Float], Array[Float]) {
  let d_h = param.d_h
  // d_o = d_h ⊙ tanh(C_t)
  let d_o : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    d_o[i] = d_h_t[i] * cache.tanh_c_t[i]
  }
  // d_c_from_h = d_h ⊙ o ⊙ (1 - tanh²(C_t))
  let d_c_from_h : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    let one_minus_tanh_sq = 1.0F - cache.tanh_c_t[i] * cache.tanh_c_t[i]
    d_c_from_h[i] = d_h_t[i] * cache.o[i] * one_minus_tanh_sq
  }
  // Combine with upstream d_c_t.
  let d_c : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    d_c[i] = d_c_t[i] + d_c_from_h[i]
  }
  // d_C̃ = d_c ⊙ i_t
  let d_c_tilde : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    d_c_tilde[i] = d_c[i] * cache.ig[i]
  }
  // d_i = d_c ⊙ C̃_t
  let d_ig : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    d_ig[i] = d_c[i] * cache.c_tilde[i]
  }
  // d_f = d_c ⊙ C_{t-1}
  let d_f : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    d_f[i] = d_c[i] * cache.c_prev[i]
  }
  // d_C_{t-1} = d_c ⊙ f_t
  let d_c_prev : Array[Float] = Array::make(d_h, 0.0F)
  for i in 0..<d_h {
    d_c_prev[i] = d_c[i] * cache.f[i]
  }
  // Concatenated input for linear backward through each gate.
  let cat = vec_concat(cache.h_prev, cache.x)
  let d_pre_f = matvec_backward_acc(param.w_f, grad.d_b_f, grad.d_w_f, cat, d_f)
  let d_pre_i = matvec_backward_acc(param.w_i, grad.d_b_i, grad.d_w_i, cat, d_ig)
  let d_pre_c = matvec_backward_acc(
    param.w_c,
    grad.d_b_c,
    grad.d_w_c,
    cat,
    d_c_tilde,
  )
  let d_pre_o = matvec_backward_acc(param.w_o, grad.d_b_o, grad.d_w_o, cat, d_o)
  let d_pre : Array[Float] = Array::make(cat.length(), 0.0F)
  for i in 0..<cat.length() {
    d_pre[i] = d_pre_f[i] + d_pre_i[i] + d_pre_c[i] + d_pre_o[i]
  }
  let (d_h_prev, d_x) = vec_split(d_pre, param.d_h)
  (d_h_prev, d_x, d_c_prev)
}

///|
/// Apply accumulated gradients to parameters with simple SGD.
pub fn lstm_cell_sgd_step(
  param : LstmCellParam,
  grad : LstmCellGrad,
  lr : Float,
) -> Unit {
  for i in 0..<param.d_h {
    let in_dim = param.w_f[i].length()
    for j in 0..<in_dim {
      param.w_f[i][j] = param.w_f[i][j] - lr * grad.d_w_f[i][j]
      param.w_i[i][j] = param.w_i[i][j] - lr * grad.d_w_i[i][j]
      param.w_c[i][j] = param.w_c[i][j] - lr * grad.d_w_c[i][j]
      param.w_o[i][j] = param.w_o[i][j] - lr * grad.d_w_o[i][j]
      grad.d_w_f[i][j] = 0.0F
      grad.d_w_i[i][j] = 0.0F
      grad.d_w_c[i][j] = 0.0F
      grad.d_w_o[i][j] = 0.0F
    }
    param.b_f[i] = param.b_f[i] - lr * grad.d_b_f[i]
    param.b_i[i] = param.b_i[i] - lr * grad.d_b_i[i]
    param.b_c[i] = param.b_c[i] - lr * grad.d_b_c[i]
    param.b_o[i] = param.b_o[i] - lr * grad.d_b_o[i]
    grad.d_b_f[i] = 0.0F
    grad.d_b_i[i] = 0.0F
    grad.d_b_c[i] = 0.0F
    grad.d_b_o[i] = 0.0F
  }
}