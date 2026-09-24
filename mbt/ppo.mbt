// ppo.mbt — PPO (clipped surrogate) on GridWorld (v0.35.0).
//
// Proximal Policy Optimization with the clipped surrogate objective:
//
//   r_t(θ) = π_θ(a_t | s_t) / π_θ_old(a_t | s_t)
//   L_clip(θ) = E[ min( r_t · A_t, clip(r_t, 1-ε, 1+ε) · A_t ) ]
//
// We use A_t = G_t (full Monte-Carlo return) as the advantage
// (no value baseline; simplest version). The clipped term removes
// gradient signal once the importance ratio leaves [1-ε, 1+ε],
// keeping policy updates conservative.
//
// Gradient of the (negative) loss per transition:
//   g_clip[i, j] = -A_t · effective_ratio · (1{i == a_t} - π_θ(i|s_t)) · x_t[j]
// where effective_ratio = r_t if unclipped, else clip(r_t, 1-ε, 1+ε).
//
// Policy: `LinearSoftmaxPolicy` from reinforce.mbt. Environment:
// `GridWorld` (also from reinforce.mbt).

///|
/// A batch of transitions collected under a "frozen" policy snapshot.
pub(all) struct PpoBatch {
  states : Array[Int]
  actions : Array[Int]
  returns : Array[Float]
  old_probs : Array[Float]  // π_old(a_t | s_t) recorded at collection time
  n_episodes : Int
}

///|
/// Collect a batch of episodes under the current policy. Records
/// `old_probs[a_t | s_t]` for use as the importance-ratio denominator.
pub fn ppo_collect_batch(
  env : GridWorld,
  policy : LinearSoftmaxPolicy,
  n_episodes : Int,
  gamma : Float,
  max_steps : Int,
  seed : UInt64,
) -> PpoBatch {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let states : Array[Int] = []
  let actions : Array[Int] = []
  let returns_arr : Array[Float] = []
  let old_probs : Array[Float] = []
  for _ep in 0..<n_episodes {
    let episode = policy_rollout(env, policy, max_steps, rng)
    let ep_len = episode.states.length()
    let rets = compute_returns(episode.rewards, gamma)
    for t in 0..<ep_len {
      let s = episode.states[t]
      let a = episode.actions[t]
      let x = rl_one_hot(s, env.n_states())
      let (_logits, probs) = policy_forward(policy, x)
      states.push(s)
      actions.push(a)
      returns_arr.push(rets[t])
      old_probs.push(probs[a])
    }
  }
  { states, actions, returns: returns_arr, old_probs, n_episodes }
}

///|
/// Apply one PPO clipped-surrogate update step. Modifies
/// `policy.w` in place. `clip_eps` is typically 0.2.
pub fn ppo_update_step(
  policy : LinearSoftmaxPolicy,
  batch : PpoBatch,
  clip_eps : Float,
  lr : Float,
) -> Unit {
  let n = batch.states.length()
  let n_a = policy.n_actions
  let n_s = policy.n_states
  let lower = 1.0F - clip_eps
  let upper = 1.0F + clip_eps
  for step in 0..<n {
    let g = batch.returns[step]
    if g == 0.0F {
      continue
    }
    let a = batch.actions[step]
    let old_p = batch.old_probs[step]
    if old_p <= 0.0F {
      continue
    }
    let x = rl_one_hot(batch.states[step], n_s)
    let (_logits, probs) = policy_forward(policy, x)
    let cur_p = probs[a]
    // Importance ratio.
    let r = cur_p / old_p
    // Decide whether the unclipped branch is the active one.
    // For A = g > 0: active if r ≤ upper (otherwise clipped to upper).
    // For A = g < 0: active if r ≥ lower (otherwise clipped to lower).
    let effective = if g >= 0.0F {
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
    // d_logits[i] = (1{i == a} - probs[i]) * (-g * effective)
    // The negative sign comes from converting "maximize L_clip" to
    // "minimize -L_clip". We treat g as the advantage and apply a
    // gradient step on policy weights that moves them in the
    // direction of increased log π for positive g.
    let scale = g * effective
    for i in 0..<n_a {
      let indicator = if i == a { 1.0F } else { 0.0F }
      let d_logit = (indicator - probs[i]) * scale
      let row = policy.w[i]
      for j in 0..<n_s {
        row[j] = row[j] + lr * d_logit * x[j]
      }
    }
  }
}

///|
/// Train PPO for `n_iters` iterations, each collecting a batch of
/// `n_episodes_per_iter` episodes and applying one PPO update.
/// Returns the mean episode return over the last iteration.
pub fn train_ppo(
  env : GridWorld,
  policy : LinearSoftmaxPolicy,
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
    let batch = ppo_collect_batch(
      env, policy, n_episodes_per_iter, gamma, max_steps, s,
    )
    ppo_update_step(policy, batch, clip_eps, lr)
    // Compute mean return on the just-collected batch.
    let mut sum = 0.0F
    for r in batch.returns {
      sum = sum + r
    }
    // Returns are per-step, so divide by step count to get mean
    // per-step return (proxy for episode quality).
    let mean = sum / Float::from_int(batch.returns.length())
    last_mean = mean
    s = s + 1UL
  }
  last_mean
}