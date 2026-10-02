// sac_gru_actor.mbt — Recurrent SAC (Soft Actor-Critic) stochastic
// actor for partially-observable continuous-control POMDPs (v0.68.0).
//
// Architecture (parallel to v0.58.0 GRUDeterministicPolicy but with
// SAC-style stochastic outputs):
//
//   state_t -> MLP w1 -> ReLU -> GRU cell (over time) -> two MLP
//   branches (mean + log_std) -> tanh squash + Gaussian sampling.
//
//   x_proj_t = ReLU(w1 · state_t + b1)             ∈ R^{hidden}
//   h_t = GRU_cell(x_proj_t, h_{t-1})
//   mean_pre_t = w_mean · h_t + b_mean             ∈ R^{action_dim}
//   log_std_pre_t = w_log_std · h_t + b_log_std     ∈ R^{action_dim}
//   mean_t = squash_to_action_range(tanh(mean_pre_t))
//   a_raw_t = mean_t + exp(log_std_t) * eps_t       (eps ~ N(0, 1))
//   a_t = tanh(a_raw_t) * scale + mid              ∈ R^{action_dim}
//   log π_t = log N(a_raw_t; mean_t, log_std_t) - sum log(1 - tanh²(a_raw_t))
//
// log_std is clamped to [-20, 2] for numerical stability
// (Haarnoja 2018 SAC reference).
//
// Reference: Haarnoja et al. 2018 "Soft Actor-Critic" + Haarnoja 2019
// SAC for discrete action extension to POMDP via recurrent actor.

///|
/// Recurrent SAC stochastic actor. Two output branches (mean, log_std)
/// from the same GRU hidden state. `mean_t` is squashed to
/// [action_low, action_high]; `log_std_t` is clamped to [-20, 2].
pub struct GRUSACActor {
  state_dim : Int
  action_dim : Int
  gru_hidden : Int
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  gru : GruCellParam
  mlp_w_mean : Array[Array[Float]]
  mlp_b_mean : Array[Float]
  mlp_w_log_std : Array[Array[Float]]
  mlp_b_log_std : Array[Float]
  action_low : Float
  action_high : Float
  log_std_min : Float
  log_std_max : Float
}

///|
/// Build a fresh `GRUSACActor`. MLP weights use Xavier-normal init
/// (He-style for ReLU); GRU cell uses its own init. log_std biases
/// are init to 0 (so std starts at exp(0)=1).
pub fn GRUSACActor::new(
  state_dim : Int,
  action_dim : Int,
  gru_hidden : Int,
  action_low : Float,
  action_high : Float,
  seed : UInt64,
) -> GRUSACActor {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std1 = sqrtf(2.0F / Float::from_int(state_dim))
  let mlp_w1 = xavier_normal(gru_hidden, state_dim, std1, rng1)
  let mlp_b1 : Array[Float] = Array::make(gru_hidden, 0.0F)
  let gru = GruCellParam::new(gru_hidden, gru_hidden, seed + 4UL)
  let rng2 = Xoshiro::from_state(seed + 5UL, seed + 6UL, seed + 7UL, seed + 8UL)
  let std2 = sqrtf(2.0F / Float::from_int(gru_hidden))
  let mlp_w_mean = xavier_normal(action_dim, gru_hidden, std2, rng2)
  let mlp_b_mean : Array[Float] = Array::make(action_dim, 0.0F)
  let rng3 = Xoshiro::from_state(seed + 9UL, seed + 10UL, seed + 11UL, seed + 12UL)
  let mlp_w_log_std = xavier_normal(action_dim, gru_hidden, std2, rng3)
  let mlp_b_log_std : Array[Float] = Array::make(action_dim, 0.0F)
  {
    state_dim,
    action_dim,
    gru_hidden,
    mlp_w1,
    mlp_b1,
    gru,
    mlp_w_mean,
    mlp_b_mean,
    mlp_w_log_std,
    mlp_b_log_std,
    action_low,
    action_high,
    log_std_min : -20.0F,
    log_std_max : 2.0F,
  }
}

///|
/// Single-step stochastic actor forward. Returns
/// `(action, log_prob, new_hidden)` where:
///   - `action` is the squashed, action_range-scaled sample
///   - `log_prob` is the per-timestep log π(a_t | s_t) (Float scalar = sum of dims)
///   - `new_hidden` is the post-GRU hidden state
pub fn sac_gru_actor_step(
  policy : GRUSACActor,
  state : Array[Float],
  hidden : Array[Float],
  rng : Xoshiro,
) -> (Array[Float], Float, Array[Float]) {
  let x_proj_pre = matvec(policy.mlp_w1, policy.mlp_b1, state)
  let x_proj = relu_forward(x_proj_pre)
  let (hidden_next, _cache) = gru_cell_forward(x_proj, hidden, policy.gru)
  let mean_pre = matvec(policy.mlp_w_mean, policy.mlp_b_mean, hidden_next)
  let log_std_pre = matvec(policy.mlp_w_log_std, policy.mlp_b_log_std, hidden_next)
  let _ = _cache
  // Clamp log_std for numerical safety.
  let log_std : Array[Float] = Array::make(policy.action_dim, 0.0F)
  for i in 0..<policy.action_dim {
    let v = log_std_pre[i]
    if v < policy.log_std_min {
      log_std[i] = policy.log_std_min
    } else if v > policy.log_std_max {
      log_std[i] = policy.log_std_max
    } else {
      log_std[i] = v
    }
  }
  let scale = (policy.action_high - policy.action_low) * 0.5F
  let mid = (policy.action_high + policy.action_low) * 0.5F
  let action : Array[Float] = Array::make(policy.action_dim, 0.0F)
  let mean_tanh : Array[Float] = Array::make(policy.action_dim, 0.0F)
  for i in 0..<policy.action_dim {
    let t = tanhf(mean_pre[i])
    mean_tanh[i] = t
    action[i] = mid + t * scale
  }
  let mut log_prob = 0.0F
  for i in 0..<policy.action_dim {
    let (z1, _) = box_muller(rng)
    let eps = Float::from_double(z1)
    let std_dev = expf(log_std[i])
    let a_raw = mean_tanh[i] + std_dev * eps
    let a_squash = tanhf(a_raw)
    action[i] = mid + a_squash * scale
    // log N(a_raw; mean_tanh, std) for this dim.
    let inv_std = 1.0F / std_dev
    let z_mah = (a_raw - mean_tanh[i]) * inv_std
    let dim_log_prob = -0.5F *
      (z_mah * z_mah + 2.0F * log_std[i] + 0.9189385F)
    // tanh squash correction: - sum log(1 - tanh²(a_raw)).
    let one_minus_tanh_sq = 1.0F - a_squash * a_squash
    let safe_term = if one_minus_tanh_sq < 1.0e-6F {
      1.0e-6F
    } else {
      one_minus_tanh_sq
    }
    let squash_correction = -logf(safe_term)
    log_prob = log_prob + dim_log_prob + squash_correction
  }
  (action, log_prob, hidden_next)
}

///|
/// Helper API: sample a stochastic action and return
/// `(action, new_hidden, log_prob)` — analogous to
/// `ddpg_gru_select_action` (DDPG-style) but for the SAC stochastic
/// actor.
pub fn sac_gru_select_action(
  policy : GRUSACActor,
  state : Array[Float],
  hidden : Array[Float],
  rng : Xoshiro,
) -> (Array[Float], Array[Float], Float) {
  let (a, log_prob, hidden_next) = sac_gru_actor_step(policy, state, hidden, rng)
  (a, hidden_next, log_prob)
}