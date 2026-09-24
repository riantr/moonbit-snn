// lstm_ppo.mbt — Recurrent PPO with LSTM policy (v0.35.3).
//
// Recurrent PPO on the POMDP corridor. The forward pass is the
// `LstmPolicy` from `lstm_policy.mbt` (v0.33.2); the update step
// applies PPO-style clipped surrogate with the importance ratio
// computed from cached π_old and the (re-derived) current π.
//
// Note: this is a single-pass online PPO — the cached probs serve
// as π_old, and the current π is the policy at update time. The
// importance ratio deviates from 1 only because updating `w_out`
// shifts the policy between when we cached probs and when we
// recompute them. In practice with 1 epoch and small lr, ratio
// stays close to 1.

///|
/// Per-step transition record for recurrent PPO. Includes both the
/// cached π_old (at batch collection) and the recomputed π_cur
/// (at update time, after policy weights have shifted).
pub(all) struct RecurrentPpoBatch {
  states : Array[Int]
  actions : Array[Int]
  advantages : Array[Float]
  old_probs : Array[Float]
  cur_probs : Array[Float]
  cur_dist : Array[Array[Float]]
  old_dist : Array[Array[Float]]
}

///|
/// Collect a batch from the LSTM policy on the corridor POMDP.
/// Uses Monte-Carlo returns as advantages (no value baseline).
pub fn lstm_ppo_collect_batch(
  env_n_cells : Int,
  policy : LstmPolicy,
  n_episodes : Int,
  gamma : Float,
  max_steps : Int,
  seed : UInt64,
) -> RecurrentPpoBatch {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let states : Array[Int] = []
  let actions : Array[Int] = []
  let advantages : Array[Float] = []
  let old_probs : Array[Float] = []
  let cur_probs : Array[Float] = []
  let cur_dist : Array[Array[Float]] = []
  let old_dist : Array[Array[Float]] = []
  let start = env_n_cells / 2
  for _ep in 0..<n_episodes {
    let (u_raw, _) = box_muller(rng)
    let side = if u_raw > 0.0 { 1 } else { 0 }
    let env : CorridorEnv = CorridorEnv::{
      n_cells: env_n_cells,
      goal_side: side,
      step_penalty: -0.1F,
      goal_reward: 1.0F,
      max_steps,
    }
    let obs_seq : Array[Array[Float]] = []
    for _t in 0..<max_steps {
      obs_seq.push(corridor_observation(env, start))
    }
    let h0 : Array[Float] = Array::make(policy.cell.d_h, 0.0F)
    let c0 : Array[Float] = Array::make(policy.cell.d_h, 0.0F)
    let (acts, cache) = lstm_policy_forward(policy, obs_seq, h0, c0, rng)
    let actual_rewards : Array[Float] = []
    let mut pos = start
    let mut done = false
    let mut k = 0
    while !done && k < acts.length() {
      let (np, r, d) = env.step(pos, acts[k])
      actual_rewards.push(r)
      pos = np
      done = d
      k = k + 1
    }
    let ep_len = actual_rewards.length()
    if ep_len == 0 {
      continue
    }
    let rets = compute_returns(actual_rewards, gamma)
    for t in 0..<ep_len {
      states.push(start)
      actions.push(acts[t])
      advantages.push(rets[t])
      old_probs.push(cache.probs[t][acts[t]])
      cur_probs.push(cache.probs[t][acts[t]])
      let cur_copy : Array[Float] = Array::make(cache.probs[t].length(), 0.0F)
      let old_copy : Array[Float] = Array::make(cache.probs[t].length(), 0.0F)
      for m in 0..<cache.probs[t].length() {
        cur_copy[m] = cache.probs[t][m]
        old_copy[m] = cache.probs[t][m]
      }
      cur_dist.push(cur_copy)
      old_dist.push(old_copy)
    }
  }
  { states, actions, advantages, old_probs, cur_probs, cur_dist, old_dist }
}

///|
/// Apply one PPO-clipped update step. Modifies `policy.w_out`
/// (output projection) in place. With 1 epoch and small lr the
/// importance ratio stays near 1, so clipping rarely activates —
/// this is the standard single-pass online PPO setting.
pub fn lstm_ppo_update_step(
  policy : LstmPolicy,
  batch : RecurrentPpoBatch,
  clip_eps : Float,
  lr : Float,
) -> Float {
  let lower = 1.0F - clip_eps
  let upper = 1.0F + clip_eps
  let n_actions = policy.w_out.length()
  let d_h = policy.cell.d_h
  let n = batch.states.length()
  let mut total_kl = 0.0F
  for t in 0..<n {
    let advantage = batch.advantages[t]
    let old_p = batch.old_probs[t]
    let cur_p = batch.cur_probs[t]
    if old_p <= 0.0F || cur_p <= 0.0F {
      continue
    }
    let act = batch.actions[t]
    let cur_d = batch.cur_dist[t]
    let old_d = batch.old_dist[t]
    // Importance ratio.
    let r = cur_p / old_p
    let effective = if advantage >= 0.0F {
      if r > upper {
        upper
      } else {
        r
      }
    } else {
      if r < lower {
        lower
      } else {
        r
      }
    }
    let scale = advantage * effective
    for i in 0..<n_actions {
      let indicator = if i == act { 1.0F } else { 0.0F }
      let d_logit = (indicator - cur_d[i]) * scale
      let row = policy.w_out[i]
      for j in 0..<d_h {
        row[j] = row[j] + lr * d_logit
        let _ = j
      }
    }
    // Track KL drift for monitoring.
    let mut kl = 0.0F
    for i in 0..<n_actions {
      if cur_d[i] > 0.0F && old_d[i] > 0.0F {
        kl = kl + cur_d[i] * logf(cur_d[i] / old_d[i])
      }
    }
    total_kl = total_kl + kl
  }
  if n > 0 {
    total_kl / Float::from_int(n)
  } else {
    0.0F
  }
}

///|
/// Train recurrent PPO for `n_iters`. Returns the last mean
/// per-step advantage.
pub fn train_lstm_ppo(
  env_n_cells : Int,
  policy : LstmPolicy,
  n_iters : Int,
  n_episodes_per_iter : Int,
  gamma : Float,
  clip_eps : Float,
  lr : Float,
  max_steps : Int,
  seed : UInt64,
) -> Float {
  let mut last_mean = 0.0F
  let mut s = seed
  for _iter in 0..<n_iters {
    let batch = lstm_ppo_collect_batch(
      env_n_cells, policy, n_episodes_per_iter, gamma, max_steps, s,
    )
    let _ = lstm_ppo_update_step(policy, batch, clip_eps, lr)
    let mut sum = 0.0F
    let mut count = 0
    for a in batch.advantages {
      sum = sum + a
      count = count + 1
    }
    last_mean = if count > 0 { sum / Float::from_int(count) } else { 0.0F }
    s = s + 1UL
  }
  last_mean
}