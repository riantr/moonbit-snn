// trajectory_transformer_trainer.mbt — Trajectory Transformer offline
// training step (v0.82.0).
//
// Performs one supervised learning step on a sampled trajectory
// window: total loss = MSE on next_state + MSE on reward + MSE on
// done + MSE on value.
//
//   L_state = (1/N_s) · Σ_t || pred_next_state_t - target_next_state_t ||²
//   L_reward = (1/N_s) · Σ_t (pred_reward_t - target_reward_t)²
//   L_done = (1/N_s) · Σ_t (pred_done_t - target_done_t)²
//   L_value = (1/N_s) · Σ_t (pred_value_t - target_value_t)²
//   L_total = L_state + L_reward + L_done + L_value
//
// Scope of v0.82.0:
//   - tt_mse_loss_4heads: scalar total loss
//   - tt_train_step: one SGD step on the TT's 4 prediction heads
//     (state_w/b, reward_w/b, done_w/b, value_w/b). Returns the updated
//     TrajectoryTransformer + scalar loss.
//   - The TT embeddings + GTrXL backbone gradients are deferred
//     (BPTT through the GTrXL block not yet implemented — see Batch G
//     deferral).
//
// Note: `flatten_2d` / `unflatten_2d` helpers are defined in
// `decision_transformer_trainer.mbt` (v0.79.0) and reused here.

///|
/// Compute the total MSE loss across all 4 heads. Returns
///   L_state + L_reward + L_done + L_value
/// as a single Float.
pub fn tt_mse_loss_4heads(
  pred_next_state_seq : Array[Float],
  pred_reward_seq : Array[Float],
  pred_done_seq : Array[Float],
  pred_value_seq : Array[Float],
  target_next_state_seq : Array[Float],
  target_reward_seq : Array[Float],
  target_done_seq : Array[Float],
  target_value_seq : Array[Float],
  n_elements : Int,
  state_dim : Int,
) -> Float {
  if n_elements <= 0 {
    return 0.0F
  }
  let n_state = n_elements * state_dim
  let n_scalar = n_elements
  let mut sum_sq_state = 0.0F
  let mut sum_sq_reward = 0.0F
  let mut sum_sq_done = 0.0F
  let mut sum_sq_value = 0.0F
  for i in 0..<n_state {
    let d = pred_next_state_seq[i] - target_next_state_seq[i]
    sum_sq_state = sum_sq_state + d * d
  }
  for i in 0..<n_scalar {
    let dr = pred_reward_seq[i] - target_reward_seq[i]
    sum_sq_reward = sum_sq_reward + dr * dr
    let dd = pred_done_seq[i] - target_done_seq[i]
    sum_sq_done = sum_sq_done + dd * dd
    let dv = pred_value_seq[i] - target_value_seq[i]
    sum_sq_value = sum_sq_value + dv * dv
  }
  let scale_state = 1.0F / Float::from_int(n_state)
  let scale_scalar = 1.0F / Float::from_int(n_scalar)
  scale_state * sum_sq_state +
  scale_scalar * sum_sq_reward +
  scale_scalar * sum_sq_done +
  scale_scalar * sum_sq_value
}

///|
/// Per-head MSE gradient: (2 / N) · (pred - target). All shapes flat.
fn tt_mse_grad_flat(
  pred : Array[Float],
  target : Array[Float],
  n : Int,
) -> Array[Float] {
  let grad : Array[Float] = Array::make(n, 0.0F)
  if n <= 0 {
    return grad
  }
  let scale = 2.0F / Float::from_int(n)
  for i in 0..<n {
    grad[i] = scale * (pred[i] - target[i])
  }
  grad
}

///|
/// Compute the gradient of the head-MSE losses w.r.t. the head weights.
/// For each head:
///   d_w[i][k] = Σ_t d_out_t[i] · hidden_t[k]
///   d_b[i] = Σ_t d_out_t[i]
/// Returns (d_state_w, d_state_b, d_reward_w, d_reward_b, d_done_w,
/// d_done_b, d_value_w, d_value_b).
fn tt_head_grads(
  grad_state : Array[Float],
  grad_reward : Array[Float],
  grad_done : Array[Float],
  grad_value : Array[Float],
  hidden_seq : Array[Float],
  seq_len : Int,
  state_dim : Int,
  d_model : Int,
) -> (
  Array[Array[Float]],
  Array[Float],
  Array[Array[Float]],
  Float,
  Array[Array[Float]],
  Float,
  Array[Array[Float]],
  Float,
) {
  let d_state_w : Array[Array[Float]] = Array::make(
    state_dim, Array::make(d_model, 0.0F),
  )
  let d_state_b : Array[Float] = Array::make(state_dim, 0.0F)
  let d_reward_w : Array[Array[Float]] = Array::make(
    1, Array::make(d_model, 0.0F),
  )
  let d_done_w : Array[Array[Float]] = Array::make(
    1, Array::make(d_model, 0.0F),
  )
  let d_value_w : Array[Array[Float]] = Array::make(
    1, Array::make(d_model, 0.0F),
  )
  let mut d_reward_b = 0.0F
  let mut d_done_b = 0.0F
  let mut d_value_b = 0.0F
  for t in 0..<seq_len {
    let off = t * d_model
    let st_off = t * state_dim
    for i in 0..<state_dim {
      let g = grad_state[st_off + i]
      d_state_b[i] = d_state_b[i] + g
      for k in 0..<d_model {
        d_state_w[i][k] = d_state_w[i][k] + g * hidden_seq[off + k]
      }
    }
    let gr = grad_reward[t]
    let gd = grad_done[t]
    let gv = grad_value[t]
    d_reward_b = d_reward_b + gr
    d_done_b = d_done_b + gd
    d_value_b = d_value_b + gv
    for k in 0..<d_model {
      let h = hidden_seq[off + k]
      d_reward_w[0][k] = d_reward_w[0][k] + gr * h
      d_done_w[0][k] = d_done_w[0][k] + gd * h
      d_value_w[0][k] = d_value_w[0][k] + gv * h
    }
  }
  (d_state_w, d_state_b, d_reward_w, d_reward_b, d_done_w, d_done_b, d_value_w, d_value_b)
}

///|
/// One SGD step on the TrajectoryTransformer's 4 prediction heads.
/// Returns `(updated_tt, loss_value)`.
pub fn tt_train_step(
  tt : TrajectoryTransformer,
  hidden_seq : Array[Float],
  pred_next_state_seq : Array[Float],
  pred_reward_seq : Array[Float],
  pred_done_seq : Array[Float],
  pred_value_seq : Array[Float],
  target_next_state_seq : Array[Float],
  target_reward_seq : Array[Float],
  target_done_seq : Array[Float],
  target_value_seq : Array[Float],
  seq_len : Int,
  lr : Float,
) -> (TrajectoryTransformer, Float) {
  let state_dim = tt.state_dim
  let n_elements = seq_len
  let loss = tt_mse_loss_4heads(
    pred_next_state_seq, pred_reward_seq, pred_done_seq, pred_value_seq,
    target_next_state_seq, target_reward_seq, target_done_seq,
    target_value_seq, n_elements, state_dim,
  )
  let grad_state = tt_mse_grad_flat(
    pred_next_state_seq, target_next_state_seq, seq_len * state_dim,
  )
  let grad_reward = tt_mse_grad_flat(pred_reward_seq, target_reward_seq, seq_len)
  let grad_done = tt_mse_grad_flat(pred_done_seq, target_done_seq, seq_len)
  let grad_value = tt_mse_grad_flat(pred_value_seq, target_value_seq, seq_len)
  let (d_state_w, d_state_b, d_reward_w, d_reward_b, d_done_w, d_done_b, d_value_w, d_value_b) = tt_head_grads(
    grad_state, grad_reward, grad_done, grad_value,
    hidden_seq, seq_len, state_dim, tt.d_model,
  )
  // SGD on state head (matrix + vector bias).
  let (new_state_w, new_state_b) = sgd_update_arrays(
    flatten_2d(tt.state_w), tt.state_b, flatten_2d(d_state_w), d_state_b, lr,
  )
  // SGD on the 3 scalar heads — wrap each scalar bias as a 1-elem array.
  let tt_reward_b_arr : Array[Float] = [tt.reward_b]
  let (new_reward_w, new_reward_b_arr) = sgd_update_arrays(
    flatten_2d(tt.reward_w), tt_reward_b_arr, flatten_2d(d_reward_w), [d_reward_b],
    lr,
  )
  let tt_done_b_arr : Array[Float] = [tt.done_b]
  let (new_done_w, new_done_b_arr) = sgd_update_arrays(
    flatten_2d(tt.done_w), tt_done_b_arr, flatten_2d(d_done_w), [d_done_b],
    lr,
  )
  let tt_value_b_arr : Array[Float] = [tt.value_b]
  let (new_value_w, new_value_b_arr) = sgd_update_arrays(
    flatten_2d(tt.value_w), tt_value_b_arr, flatten_2d(d_value_w), [d_value_b],
    lr,
  )
  let updated : TrajectoryTransformer = {
    ..tt,
    state_w: unflatten_2d(new_state_w, state_dim, tt.d_model),
    state_b: new_state_b,
    reward_w: unflatten_2d(new_reward_w, 1, tt.d_model),
    reward_b: new_reward_b_arr[0],
    done_w: unflatten_2d(new_done_w, 1, tt.d_model),
    done_b: new_done_b_arr[0],
    value_w: unflatten_2d(new_value_w, 1, tt.d_model),
    value_b: new_value_b_arr[0],
  }
  (updated, loss)
}
