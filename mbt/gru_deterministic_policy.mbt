// gru_deterministic_policy.mbt — Recurrent deterministic policy for
// partially-observable continuous-control RL (v0.58.0).
//
// Architecture: MLP w1 (state → hidden) → ReLU → GRU cell (over time)
// → MLP w2 (hidden → action) → tanh squash to [action_low, action_high].
//
//   state_t ∈ R^{state_dim}
//   x_proj_t = ReLU(w1 · state_t + b1)              ∈ R^{gru_hidden}
//   hidden_t = GRU_cell(x_proj_t, hidden_{t-1})    ∈ R^{gru_hidden}
//   a_pre_t  = w2 · hidden_t + b2                  ∈ R^{action_dim}
//   action_t = tanh(a_pre_t) * (high-low)/2
//            + (high+low)/2                         ∈ R^{action_dim}
//
// This is the actor counterpart of `DeterministicPolicy` (v0.54.0) for
// environments where the observation alone is insufficient and a
// recurrent summary of past observations is needed (e.g. POMDPs,
// partial-observable continuous control).
//
// Reference: Hausknecht & Stone 2015 "Deep Recurrent Q-Learning for
// Partially Observable MDPs" (the recurrent actor pattern).

///|
/// Recurrent deterministic policy. The GRU hidden dimension equals
/// the MLP hidden dimension so the ReLU-projected state and the GRU
/// cell input/output match.
pub struct GRUDeterministicPolicy {
  state_dim : Int
  action_dim : Int
  gru_hidden : Int
  mlp_w1 : Array[Array[Float]]
  mlp_b1 : Array[Float]
  gru : GruCellParam
  mlp_w2 : Array[Array[Float]]
  mlp_b2 : Array[Float]
  action_low : Float
  action_high : Float
}

///|
/// Build a fresh GRUDeterministicPolicy. The MLP weights get
/// Xavier-normal init scaled by `sqrtf(2.0 / fan_in)` (He-style for
/// ReLU), and the GRU cell reuses its own init. Zero biases.
/// `action_low` / `action_high` define the squash range.
pub fn GRUDeterministicPolicy::new(
  state_dim : Int,
  action_dim : Int,
  gru_hidden : Int,
  action_low : Float,
  action_high : Float,
  seed : UInt64,
) -> GRUDeterministicPolicy {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let mlp_w1_std = sqrtf(2.0F / Float::from_int(state_dim))
  let mlp_w1 = xavier_normal(gru_hidden, state_dim, mlp_w1_std, rng1)
  let mlp_b1 : Array[Float] = Array::make(gru_hidden, 0.0F)
  let gru = GruCellParam::new(gru_hidden, gru_hidden, seed + 4UL)
  let rng2 = Xoshiro::from_state(seed + 5UL, seed + 6UL, seed + 7UL, seed + 8UL)
  let mlp_w2_std = sqrtf(2.0F / Float::from_int(gru_hidden))
  let mlp_w2 = xavier_normal(action_dim, gru_hidden, mlp_w2_std, rng2)
  let mlp_b2 : Array[Float] = Array::make(action_dim, 0.0F)
  {
    state_dim,
    action_dim,
    gru_hidden,
    mlp_w1,
    mlp_b1,
    gru,
    mlp_w2,
    mlp_b2,
    action_low,
    action_high,
  }
}

///|
/// Single-step forward. `obs` is the current observation (length
/// state_dim), `hidden` is the previous GRU hidden state (length
/// gru_hidden). Returns `(action, new_hidden)` where action is the
/// squashed continuous action (length action_dim).
pub fn gru_deterministic_policy_step(
  policy : GRUDeterministicPolicy,
  obs : Array[Float],
  hidden : Array[Float],
) -> (Array[Float], Array[Float]) {
  // x_proj = ReLU(w1 · obs + b1)
  let x_proj_pre = matvec(policy.mlp_w1, policy.mlp_b1, obs)
  let x_proj = relu_forward(x_proj_pre)
  // hidden_next = GRU_cell(x_proj, hidden)
  let (hidden_next, _cache) = gru_cell_forward(x_proj, hidden, policy.gru)
  // a_pre = w2 · hidden_next + b2
  let a_pre = matvec(policy.mlp_w2, policy.mlp_b2, hidden_next)
  // squash: action = tanh(a_pre) * (high - low) / 2 + (high + low) / 2
  let half_range = (policy.action_high - policy.action_low) * 0.5F
  let mid = (policy.action_high + policy.action_low) * 0.5F
  let action : Array[Float] = Array::make(policy.action_dim, 0.0F)
  for i in 0..<policy.action_dim {
    action[i] = tanhf(a_pre[i]) * half_range + mid
  }
  (action, hidden_next)
}

///|
/// Sequence forward. `obs_seq` is flat row-major of shape
/// `[seq_len × state_dim]`. `hidden_init` is the initial GRU hidden
/// state (length gru_hidden; pass zeros to start fresh). Returns
/// `(action_seq, final_hidden)` where `action_seq` is flat
/// `[seq_len × action_dim]`.
pub fn gru_deterministic_policy_seq_forward(
  policy : GRUDeterministicPolicy,
  obs_seq : Array[Float],
  seq_len : Int,
  hidden_init : Array[Float],
) -> (Array[Float], Array[Float]) {
  let action_seq : Array[Float] = Array::make(seq_len * policy.action_dim, 0.0F)
  let mut hidden = hidden_init
  for t in 0..<seq_len {
    let obs_off = t * policy.state_dim
    let obs : Array[Float] = Array::make(policy.state_dim, 0.0F)
    for k in 0..<policy.state_dim {
      obs[k] = obs_seq[obs_off + k]
    }
    let (action, new_hidden) = gru_deterministic_policy_step(policy, obs, hidden)
    let act_off = t * policy.action_dim
    for k in 0..<policy.action_dim {
      action_seq[act_off + k] = action[k]
    }
    hidden = new_hidden
  }
  (action_seq, hidden)
}
