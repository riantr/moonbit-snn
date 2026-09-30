// noisy_qnet.mbt — Noisy Q-network (v0.43.1).
//
// Fortunato et al. 2018 §3.2: replace ε-greedy exploration with a
// stacked-NoisyLinear Q-network. The two hidden layers use
// factorised Gaussian noise on their weights/biases; the action
// selection = argmax over the noisy Q-values. At evaluation time,
// only the μ weights are used (eval mode = pure LinearQNet forward).
//
// Architecture (matches the standard CartPole/DQN shape used in
// v0.36.0 DQN):
//   layer1 : NoisyLinear(in=n_states, out=hidden) + ReLU
//   layer2 : NoisyLinear(in=hidden,    out=n_actions)
//   output  : Q[a] for each action

///|
/// Two-hidden-layer Noisy Q-network (no conv layers — matches the
/// 1D-state DQN shape used by `dqn.mbt::LinearQNet`).
pub struct NoisyQNet {
  layer1 : NoisyLinearParam
  layer2 : NoisyLinearParam
  n_states : Int
  hidden : Int
  n_actions : Int
}

///|
/// Build a fresh NoisyQNet with both layers initialized uniformly in
/// [-1/√fan_in, +1/√fan_in] and σ weights = 0.017 (paper default).
pub fn NoisyQNet::new(
  n_states : Int,
  hidden : Int,
  n_actions : Int,
  seed : UInt64,
) -> NoisyQNet {
  // Use a single RNG whose state advances through both inits.
  let rng = Xoshiro::new(seed)
  let layer1 = NoisyLinearParam::init(n_states, hidden, rng)
  let layer2 = NoisyLinearParam::init(hidden, n_actions, rng)
  { layer1, layer2, n_states, hidden, n_actions }
}

///|
/// Forward pass with sampled noise. Returns `Array[Float]` of length
/// `n_actions` (Q-values for the single input state).
pub fn noisy_q_forward(
  net : NoisyQNet,
  x : Array[Float],
  rng : Xoshiro,
) -> Array[Float] {
  // layer1: n_states -> hidden
  let h1 = noisy_linear_forward(x, 1, net.layer1, rng)
  // ReLU on hidden.
  let h1_relu : Array[Float] = Array::make(net.hidden, 0.0F)
  for i in 0..<net.hidden {
    h1_relu[i] = if h1[i] > 0.0F { h1[i] } else { 0.0F }
  }
  // layer2: hidden -> n_actions
  let q = noisy_linear_forward(h1_relu, 1, net.layer2, rng)
  q
}

///|
/// Deterministic forward (no noise). Used at evaluation time and
/// when the caller wants pure μ weights. Equivalent to running the
/// same architecture with all `σ = 0`.
pub fn noisy_q_forward_eval(net : NoisyQNet, x : Array[Float]) -> Array[Float] {
  let h1 = noisy_linear_forward_eval(x, 1, net.layer1)
  let h1_relu : Array[Float] = Array::make(net.hidden, 0.0F)
  for i in 0..<net.hidden {
    h1_relu[i] = if h1[i] > 0.0F { h1[i] } else { 0.0F }
  }
  let q = noisy_linear_forward_eval(h1_relu, 1, net.layer2)
  q
}

///|
/// Greedy action under noise (training): `argmax noisy_q_forward`.
///
/// The paper draws an independent noise sample per forward call —
/// this is what drives exploration. The action selection is still
/// greedy, but the Q-values themselves are stochastic.
pub fn noisy_q_argmax(
  net : NoisyQNet,
  x : Array[Float],
  rng : Xoshiro,
) -> Int {
  let q = noisy_q_forward(net, x, rng)
  let mut best = 0
  let mut best_v = q[0]
  for i in 1..<q.length() {
    if q[i] > best_v {
      best_v = q[i]
      best = i
    }
  }
  best
}

///|
/// Greedy action under μ-only weights (evaluation / testing).
pub fn noisy_q_argmax_eval(net : NoisyQNet, x : Array[Float]) -> Int {
  let q = noisy_q_forward_eval(net, x)
  let mut best = 0
  let mut best_v = q[0]
  for i in 1..<q.length() {
    if q[i] > best_v {
      best_v = q[i]
      best = i
    }
  }
  best
}

///|
/// Total trainable parameters (μ + σ for both NoisyLinear layers).
pub fn noisy_qnet_param_count(net : NoisyQNet) -> Int {
  noisy_linear_param_count(net.layer1) + noisy_linear_param_count(net.layer2)
}