// decision_transformer_trainer.mbt — Decision Transformer offline
// training step (v0.79.0).
//
// Performs one supervised learning step on a sampled trajectory window:
//   loss = (1 / N) · Σ_t || a_pre_t - a_t ||²
// where `a_pre_t` is the DT's action-head prediction and `a_t` is the
// ground-truth action from the trajectory buffer.
//
// Scope of v0.79.0:
//   - dt_mse_loss: scalar MSE between predicted and actual action seqs
//   - dt_train_step: one SGD step on the DT's action head (action_w,
//     action_b). Returns the updated DecisionTransformer + scalar loss.
//   - The DT embeddings + GTrXL backbone gradients are deferred (BPTT
//     through GTrXL block not yet implemented; see Batch G deferral).
//
// Reference: Chen et al. 2021, Section 4 "Training": pure supervised
// learning with MSE on action prediction, no critic, no bootstrapping.

///|
/// Compute the per-element MSE loss between predicted and target action
/// sequences. Shapes: both are flat `[seq_len × action_dim]`. Returns
/// (1 / N) · Σ (pred - target)² as a Float.
pub fn dt_mse_loss(
  predicted_action_seq : Array[Float],
  target_action_seq : Array[Float],
  n_elements : Int,
) -> Float {
  if n_elements <= 0 {
    return 0.0F
  }
  let mut sum_sq = 0.0F
  for i in 0..<n_elements {
    let d = predicted_action_seq[i] - target_action_seq[i]
    sum_sq = sum_sq + d * d
  }
  sum_sq / Float::from_int(n_elements)
}

///|
/// Compute the gradient of the per-element MSE loss w.r.t. the action
/// head output. d_loss/d_a_pre = (1 / N) · 2 · (pred - target) = (2 / N) · d.
fn dt_mse_grad_action(
  predicted_action_seq : Array[Float],
  target_action_seq : Array[Float],
  n_elements : Int,
) -> Array[Float] {
  let grad : Array[Float] = Array::make(n_elements, 0.0F)
  if n_elements <= 0 {
    return grad
  }
  let scale = 2.0F / Float::from_int(n_elements)
  for i in 0..<n_elements {
    grad[i] = scale * (predicted_action_seq[i] - target_action_seq[i])
  }
  grad
}

///|
/// Compute the gradient of the MSE loss w.r.t. the action head weights.
/// `d_action_w[i][k] = Σ_t d_a_t[i] · hidden_t[k]`.
/// `d_action_b[i] = Σ_t d_a_t[i]`.
fn dt_action_head_grad(
  grad_action : Array[Float],
  hidden_seq : Array[Float],
  seq_len : Int,
  action_dim : Int,
  d_model : Int,
) -> (Array[Array[Float]], Array[Float]) {
  let d_w : Array[Array[Float]] = Array::make(
    action_dim, Array::make(d_model, 0.0F),
  )
  let d_b : Array[Float] = Array::make(action_dim, 0.0F)
  for t in 0..<seq_len {
    let off = t * d_model
    let a_off = t * action_dim
    for i in 0..<action_dim {
      let g = grad_action[a_off + i]
      d_b[i] = d_b[i] + g
      for k in 0..<d_model {
        d_w[i][k] = d_w[i][k] + g * hidden_seq[off + k]
      }
    }
  }
  (d_w, d_b)
}

///|
/// One SGD step on the DecisionTransformer's action head. Updates
/// `action_w` and `action_b` via plain gradient descent. The DT
/// embeddings + GTrXL backbone gradients are deferred (BPTT through
/// the GTrXL block not yet implemented — see Batch G deferral).
/// Returns `(updated_dt, loss_value)`.
pub fn dt_train_step(
  dt : DecisionTransformer,
  prev_action_seq : Array[Float],
  target_action_seq : Array[Float],
  hidden_seq : Array[Float],
  seq_len : Int,
  lr : Float,
) -> (DecisionTransformer, Float) {
  // 1. Forward: get the predicted action sequence from dt_forward's
  //    intermediate (caller already computed hidden_seq).
  //    To avoid recomputing forward here, the caller passes the
  //    forward's predicted_action_seq via `prev_action_seq`.
  // 2. Loss.
  let n_elements = seq_len * dt.action_dim
  let loss = dt_mse_loss(prev_action_seq, target_action_seq, n_elements)
  // 3. Gradient w.r.t. action head.
  let grad_action = dt_mse_grad_action(prev_action_seq, target_action_seq, n_elements)
  let (d_w, d_b) = dt_action_head_grad(
    grad_action, hidden_seq, seq_len, dt.action_dim, dt.d_model,
  )
  // 4. SGD step on action_w, action_b.
  let (new_action_w, new_action_b) = sgd_update_arrays(
    flatten_2d(dt.action_w), dt.action_b, flatten_2d(d_w), d_b, lr,
  )
  let new_action_w_2d = unflatten_2d(new_action_w, dt.action_dim, dt.d_model)
  let updated : DecisionTransformer = { ..dt, action_w: new_action_w_2d, action_b: new_action_b }
  (updated, loss)
}

// Helper: flatten a 2D row-major Array[Array[Float]] into a 1D Array[Float].
fn flatten_2d(a : Array[Array[Float]]) -> Array[Float] {
  let mut total = 0
  for i in 0..<a.length() {
    total = total + a[i].length()
  }
  let out : Array[Float] = Array::make(total, 0.0F)
  let mut off = 0
  for i in 0..<a.length() {
    for k in 0..<a[i].length() {
      out[off + k] = a[i][k]
    }
    off = off + a[i].length()
  }
  out
}

// Helper: reshape 1D back to 2D row-major.
fn unflatten_2d(a : Array[Float], rows : Int, cols : Int) -> Array[Array[Float]] {
  let out : Array[Array[Float]] = Array::make(
    rows, Array::make(cols, 0.0F),
  )
  let mut off = 0
  for i in 0..<rows {
    for k in 0..<cols {
      out[i][k] = a[off + k]
    }
    off = off + cols
  }
  out
}