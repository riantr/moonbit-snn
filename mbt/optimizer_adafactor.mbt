// optimizer_adafactor.mbt — Adafactor optimizer (Shazeer 2018).
//
// Adafactor factorizes the second-moment matrix V of Adam into two
// 1D vectors (row-mean and column-mean of squared gradients) for
// memory efficiency. Reconstruction:
//
//   V_ij ≈ R_i · C_j / mean(R)
//
// where:
//   R_i = mean over j of G[i, j]^2
//   C_j = mean over i of G[i, j]^2
//
// so that sum_{i,j} V_ij = sum_{i,j} G[i, j]^2 (matches Adam).
//
// Update for 2D weight:
//
//   v_row[i]   = β2 · v_row[i]   + (1 - β2) · mean_j(G[i, j]^2)
//   v_col[j]   = β2 · v_col[j]   + (1 - β2) · mean_i(G[i, j]^2)
//   V_hat[i,j] = v_row[i] · v_col[j] / mean(v_row)
//   weight[i,j] -= lr · G[i, j] / sqrt(V_hat[i, j] + eps)
//
// For 1D parameters (biases, gamma/beta) where G is small, we just
// use a simple RMS:
//
//   v[i]   = β2 · v[i] + (1 - β2) · G[i]^2
//   bias[i] -= lr · G[i] / sqrt(v[i] + eps)
//
// No bias correction needed (Adafactor uses EMA like Adam but with
// the factorisation giving natural scaling).

///|
/// Adafactor state for a 2D weight matrix.
pub struct Adafactor2DState {
  v_row : Array[Float]
  v_col : Array[Float]
}

///|
/// Adafactor state for a 1D parameter (bias, gain).
pub struct Adafactor1DState {
  v : Array[Float]
}

///|
/// Construct a 2D Adafactor state.
pub fn Adafactor2DState::new(rows : Int, cols : Int) -> Adafactor2DState {
  { v_row: Array::make(rows, 0.0F), v_col: Array::make(cols, 0.0F) }
}

///|
/// Construct a 1D Adafactor state.
pub fn Adafactor1DState::new(n : Int) -> Adafactor1DState {
  { v: Array::make(n, 0.0F) }
}

///|
/// Adafactor 2D update. `beta2` defaults to 0.999 (matches Adam).
pub fn adafactor_update_2d(
  weight : Array[Float],
  d_weight : Array[Float],
  state : Adafactor2DState,
  lr : Float,
  eps? : Float = 0.000001F,
  beta2? : Float = 0.999F,
) -> Unit {
  let n = weight.length()
  let cols = state.v_col.length()
  let rows = state.v_row.length()
  if n != rows * cols {
    abort(
      "adafactor_update_2d: weight length \{n} != rows*cols \{rows * cols}",
    )
  }
  // 1) Compute current row-mean and col-mean of G^2.
  let row_g2 : Array[Float] = Array::make(rows, 0.0F)
  let col_g2 : Array[Float] = Array::make(cols, 0.0F)
  for i in 0..<rows {
    let mut acc = 0.0F
    for j in 0..<cols {
      let g = d_weight[i * cols + j]
      acc = acc + g * g
      col_g2[j] = col_g2[j] + g * g
    }
    row_g2[i] = acc / Float::from_int(cols)
  }
  for j in 0..<cols {
    col_g2[j] = col_g2[j] / Float::from_int(rows)
  }
  // 2) EMA update.
  let one_minus = 1.0F - beta2
  for i in 0..<rows {
    state.v_row[i] = beta2 * state.v_row[i] + one_minus * row_g2[i]
  }
  for j in 0..<cols {
    state.v_col[j] = beta2 * state.v_col[j] + one_minus * col_g2[j]
  }
  // 3) Mean of v_row for reconstruction normalisation.
  let mut mean_v_row = 0.0F
  for i in 0..<rows {
    mean_v_row = mean_v_row + state.v_row[i]
  }
  mean_v_row = mean_v_row / Float::from_int(rows)
  // 4) Update weight.
  for i in 0..<rows {
    for j in 0..<cols {
      let v_hat = state.v_row[i] * state.v_col[j] / (mean_v_row + eps)
      weight[i * cols + j] = weight[i * cols + j] -
        lr * d_weight[i * cols + j] / sqrtf(v_hat + eps)
    }
  }
}

///|
/// Adafactor 1D update.
pub fn adafactor_update_1d(
  bias : Array[Float],
  d_bias : Array[Float],
  state : Adafactor1DState,
  lr : Float,
  eps? : Float = 0.000001F,
  beta2? : Float = 0.999F,
) -> Unit {
  let n = bias.length()
  if d_bias.length() != n || state.v.length() != n {
    abort("adafactor_update_1d: length mismatch")
  }
  let one_minus = 1.0F - beta2
  for i in 0..<n {
    let g = d_bias[i]
    state.v[i] = beta2 * state.v[i] + one_minus * g * g
    bias[i] = bias[i] - lr * g / sqrtf(state.v[i] + eps)
  }
}

///|
/// Convenience: full layer update (weight + bias) for a 2D Linear.
pub fn adafactor_update_linear(
  weight : Array[Float],
  bias : Array[Float],
  d_weight : Array[Float],
  d_bias : Array[Float],
  w_state : Adafactor2DState,
  b_state : Adafactor1DState,
  lr : Float,
) -> Unit {
  adafactor_update_2d(weight, d_weight, w_state, lr)
  adafactor_update_1d(bias, d_bias, b_state, lr)
}