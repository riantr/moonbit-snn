// dsac.mbt — Distributional SAC (Ma et al. 2021) for discrete-action control.
//
// SAC (Haarnoja 2018) learns a Q-function and a softmax policy with an entropy
// bonus. Standard SAC uses a *scalar* Q-value; DSAC replaces the critic head
// with a *Gaussian* distribution Q(s, a) ~ N(μ(s, a), σ²(s, a)). The target
// distribution is the SAC soft-Bellman target plus noise (Gaussian mixture of
// the min-of-two critics). The critic loss is a Gaussian negative log-likelihood
// (NLL) on the sampled target scalar.
//
// Reference: Ma, Wang, Chen, Liu, Wei, "Distributional Soft Actor-Critic for
// Off-policy Reinforcement Learning", 2021.
//
// Key differences vs. `sac.mbt`:
//   - `LinearGaussianQNet` outputs μ AND σ (one Float each) per action.
//   - Critic loss: NLL of target scalar under N(μ, σ²). σ is updated explicitly
//     (not just through the gradient of an MSE loss).
//   - Actor loss: same as SAC (entropy-augmented expected return under π).

///|
/// Linear Q-network with Gaussian heads. Each (state, action) pair has its own
/// μ and σ. Stored as two parallel weight matrices + bias vectors.
pub struct LinearGaussianQNet {
  n_states : Int
  n_actions : Int
  // Layout: w_mu[a][s] = μ-coefficient for action a on state dim s.
  w_mu : Array[Array[Float]]
  b_mu : Array[Float]
  w_sigma : Array[Array[Float]]
  b_sigma : Array[Float]
}

///|
/// Construct a Gaussian critic. Initial μ is small-random, σ is initialised
/// to 1.0 + small jitter (in log-space for stability under direct σ updates).
pub fn LinearGaussianQNet::new(
  n_states : Int,
  n_actions : Int,
  seed : UInt64,
) -> LinearGaussianQNet {
  let rng = Xoshiro::from_state(seed, seed + 7UL, seed + 13UL, seed + 17UL)
  let bound = 1.0F / Float::from_int(n_states).to_double().sqrt().to_float()
  let w_mu : Array[Array[Float]] = []
  let b_mu : Array[Float] = []
  let w_sigma : Array[Array[Float]] = []
  let b_sigma : Array[Float] = []
  for a in 0..<n_actions {
    let row_mu : Array[Float] = []
    let row_s : Array[Float] = []
    for s in 0..<n_states {
      let (u, _) = box_muller(rng)
      let jitter_mu = Float::from_double(u) * bound
      let (u2, _) = box_muller(rng)
      let jitter_s = Float::from_double(u2) * 0.01F
      row_mu.push(jitter_mu)
      row_s.push(jitter_s)
    }
    w_mu.push(row_mu)
    w_sigma.push(row_s)
    let (u, _) = box_muller(rng)
    b_mu.push(Float::from_double(u) * 0.01F)
    b_sigma.push(0.0F) // log-space initial 0 → σ = 1
  }
  let _ = bound
  { n_states, n_actions, w_mu, b_mu, w_sigma, b_sigma }
}

///|
/// Forward pass: returns (mus, sigmas) where mus[a] = μ(s, a), sigmas[a] = σ(s, a).
pub fn gaussian_q_forward(
  net : LinearGaussianQNet,
  x : Array[Float],
) -> (Array[Float], Array[Float]) {
  let mus : Array[Float] = Array::make(net.n_actions, 0.0F)
  let sigmas : Array[Float] = Array::make(net.n_actions, 1.0F)
  for a in 0..<net.n_actions {
    let mut mu = net.b_mu[a]
    let mut ls = net.b_sigma[a] // log-space accumulator
    for s in 0..<net.n_states {
      mu = mu + net.w_mu[a][s] * x[s]
      ls = ls + net.w_sigma[a][s] * x[s]
    }
    mus[a] = mu
    // σ = exp(log-space); clamp to [1e-3, 10] for numerical stability.
    let exp_ls = Float::from_double(@math.exp(ls.to_double()))
    sigmas[a] = if exp_ls < 0.001F {
      0.001F
    } else if exp_ls > 10.0F {
      10.0F
    } else {
      exp_ls
    }
  }
  (mus, sigmas)
}

///|
/// Compute Gaussian NLL: -log p(y | μ, σ) = 0.5·((y-μ)/σ)² + log σ + 0.5·log(2π).
/// Returns a scalar Float.
pub fn gaussian_nll(y : Float, mu : Float, sigma : Float) -> Float {
  let diff = y - mu
  let z = diff / sigma
  let half_z2 = 0.5F * z * z
  let log_sigma = Float::from_double(@math.ln(sigma.to_double()))
  half_z2 + log_sigma + 0.9189385F // 0.5·log(2π)
}

///|
/// Compute the DSAC soft-Bellman target scalar for a transition. Returns a
/// single sample from the target distribution (a Gaussian centred on the SAC
/// soft-target of the min-of-two critics with a stochastic perturbation).
pub fn dsac_soft_target(
  q1_target : LinearGaussianQNet,
  q2_target : LinearGaussianQNet,
  policy : LinearSoftmaxPolicy,
  alpha : Float,
  next_state : Int,
  reward : Float,
  done : Bool,
  gamma : Float,
  rng : Xoshiro,
) -> Float {
  if done {
    return reward
  }
  let x_next = rl_one_hot(next_state, policy.n_states)
  let (_l, pi) = policy_forward(policy, x_next)
  let (mu1, sig1) = gaussian_q_forward(q1_target, x_next)
  let (mu2, sig2) = gaussian_q_forward(q2_target, x_next)
  let n_a = policy.n_actions
  let mut expected = 0.0F
  for i in 0..<n_a {
    let q_min_mu = if mu1[i] < mu2[i] { mu1[i] } else { mu2[i] }
    let log_pi = Float::from_double(@math.ln(pi[i].to_double()))
    expected = expected + pi[i] * (q_min_mu - alpha * log_pi)
  }
  // Stochastic perturbation: a small Gaussian sample drawn per call.
  let (z, _) = box_muller(rng)
  let eps = Float::from_double(z) * 0.1F
  reward + gamma * expected + eps
}

///|
/// Distributional SAC agent: policy + twin Gaussian critics + twin targets.
pub struct DSac {
  policy : LinearSoftmaxPolicy
  q1 : LinearGaussianQNet
  q2 : LinearGaussianQNet
  q1_target : LinearGaussianQNet
  q2_target : LinearGaussianQNet
  alpha : Float
}

///|
/// Construct a fresh DSAC agent. Online critics initialised from independent
/// seeds; targets initialised by copying online.
pub fn DSac::new(
  n_states : Int,
  n_actions : Int,
  alpha : Float,
  seed : UInt64,
) -> DSac {
  let policy = LinearSoftmaxPolicy::new(n_states, n_actions, seed)
  let q1 = LinearGaussianQNet::new(n_states, n_actions, seed + 1UL)
  let q2 = LinearGaussianQNet::new(n_states, n_actions, seed + 2UL)
  let q1_target = LinearGaussianQNet::new(n_states, n_actions, seed + 3UL)
  let q2_target = LinearGaussianQNet::new(n_states, n_actions, seed + 4UL)
  gaussian_qnet_copy(q1_target, q1)
  gaussian_qnet_copy(q2_target, q2)
  { policy, q1, q2, q1_target, q2_target, alpha }
}

///|
/// Copy weights from `src` to `dst` (all four: w_mu, b_mu, w_sigma, b_sigma).
pub fn gaussian_qnet_copy(
  dst : LinearGaussianQNet,
  src : LinearGaussianQNet,
) -> Unit {
  for a in 0..<dst.n_actions {
    dst.b_mu[a] = src.b_mu[a]
    dst.b_sigma[a] = src.b_sigma[a]
    for s in 0..<dst.n_states {
      dst.w_mu[a][s] = src.w_mu[a][s]
      dst.w_sigma[a][s] = src.w_sigma[a][s]
    }
  }
}

///|
/// Single critic update step: minimise Σ_i NLL(target_i | μ_i, σ_i) over the
/// batch. Gradient on μ and σ (via log-σ parameterisation) is computed by a
/// closed-form delta on the chosen action and zero on the others.
///
/// For simplicity, we apply the gradient only to the *chosen* action's μ and
/// log-σ. Returns mean NLL for monitoring.
pub fn dsac_critic_update(
  agent : DSac,
  states : Array[Int],
  actions : Array[Int],
  targets : Array[Float], // sampled target scalars
  lr : Float,
) -> Float {
  let n = states.length()
  let mut total_nll = 0.0F
  for k in 0..<n {
    let s = states[k]
    let a = actions[k]
    let t = targets[k]
    let x = rl_one_hot(s, agent.q1.n_states)
    let (mus1, sigs1) = gaussian_q_forward(agent.q1, x)
    let (mus2, sigs2) = gaussian_q_forward(agent.q2, x)
    let nll1 = gaussian_nll(t, mus1[a], sigs1[a])
    let nll2 = gaussian_nll(t, mus2[a], sigs2[a])
    total_nll = total_nll + nll1 + nll2
    // ∂NLL/∂μ = (μ - t)/σ²
    let dmu1 = (mus1[a] - t) / (sigs1[a] * sigs1[a])
    let dmu2 = (mus2[a] - t) / (sigs2[a] * sigs2[a])
    // ∂NLL/∂log-σ = (z² - 1), with z = (μ - t)/σ.
    let z1 = (mus1[a] - t) / sigs1[a]
    let z2 = (mus2[a] - t) / sigs2[a]
    let dlog_sigma1 = 0.5F * (z1 * z1 - 1.0F)
    let dlog_sigma2 = 0.5F * (z2 * z2 - 1.0F)
    // Update μ head of critic 1.
    agent.q1.b_mu[a] = agent.q1.b_mu[a] - lr * dmu1
    for j in 0..<agent.q1.n_states {
      agent.q1.w_mu[a][j] = agent.q1.w_mu[a][j] - lr * dmu1 * x[j]
    }
    // Update μ head of critic 2.
    agent.q2.b_mu[a] = agent.q2.b_mu[a] - lr * dmu2
    for j in 0..<agent.q2.n_states {
      agent.q2.w_mu[a][j] = agent.q2.w_mu[a][j] - lr * dmu2 * x[j]
    }
    // Update log-σ head of critic 1.
    agent.q1.b_sigma[a] = agent.q1.b_sigma[a] - lr * dlog_sigma1
    for j in 0..<agent.q1.n_states {
      agent.q1.w_sigma[a][j] = agent.q1.w_sigma[a][j] - lr * dlog_sigma1 * x[j]
    }
    // Update log-σ head of critic 2.
    agent.q2.b_sigma[a] = agent.q2.b_sigma[a] - lr * dlog_sigma2
    for j in 0..<agent.q2.n_states {
      agent.q2.w_sigma[a][j] = agent.q2.w_sigma[a][j] - lr * dlog_sigma2 * x[j]
    }
  }
  total_nll / Float::from_int(n)
}

///|
/// Soft (Polyak) target update: target ← (1 - τ) · target + τ · online.
pub fn dsac_soft_update(agent : DSac, tau : Float) -> Unit {
  for a in 0..<agent.q1.n_actions {
    agent.q1_target.b_mu[a] = (1.0F - tau) * agent.q1_target.b_mu[a] +
      tau * agent.q1.b_mu[a]
    agent.q2_target.b_mu[a] = (1.0F - tau) * agent.q2_target.b_mu[a] +
      tau * agent.q2.b_mu[a]
    agent.q1_target.b_sigma[a] = (1.0F - tau) * agent.q1_target.b_sigma[a] +
      tau * agent.q1.b_sigma[a]
    agent.q2_target.b_sigma[a] = (1.0F - tau) * agent.q2_target.b_sigma[a] +
      tau * agent.q2.b_sigma[a]
    for s in 0..<agent.q1.n_states {
      agent.q1_target.w_mu[a][s] = (1.0F - tau) * agent.q1_target.w_mu[a][s] +
        tau * agent.q1.w_mu[a][s]
      agent.q2_target.w_mu[a][s] = (1.0F - tau) * agent.q2_target.w_mu[a][s] +
        tau * agent.q2.w_mu[a][s]
      agent.q1_target.w_sigma[a][s] = (1.0F - tau) * agent.q1_target.w_sigma[a][s] +
        tau * agent.q1.w_sigma[a][s]
      agent.q2_target.w_sigma[a][s] = (1.0F - tau) * agent.q2_target.w_sigma[a][s] +
        tau * agent.q2.w_sigma[a][s]
    }
  }
}

///|
/// Sample action from the policy (uses `sac_sample_action`).
pub fn dsac_sample_action(
  agent : DSac,
  state : Int,
  rng : Xoshiro,
) -> Int {
  sac_sample_action(agent.policy, state, rng)
}