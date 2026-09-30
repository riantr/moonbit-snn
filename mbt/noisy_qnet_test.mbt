// noisy_qnet_test.mbt — tests for v0.43.1 NoisyQNet.

///|
test "noisy_qnet_new_shapes" {
  let n = NoisyQNet::new(4, 8, 3, 42UL)
  if n.n_states != 4 {
    fail("n_states = \{n.n_states}, expected 4")
  }
  if n.hidden != 8 {
    fail("hidden = \{n.hidden}, expected 8")
  }
  if n.n_actions != 3 {
    fail("n_actions = \{n.n_actions}, expected 3")
  }
  if n.layer1.weight_mu.length() != 32 {
    fail("layer1.weight_mu.length() = \{n.layer1.weight_mu.length()}, expected 32")
  }
  if n.layer2.weight_mu.length() != 24 {
    fail("layer2.weight_mu.length() = \{n.layer2.weight_mu.length()}, expected 24")
  }
  if n.layer1.bias_mu.length() != 8 {
    fail("layer1.bias_mu.length() = \{n.layer1.bias_mu.length()}, expected 8")
  }
  if n.layer2.bias_mu.length() != 3 {
    fail("layer2.bias_mu.length() = \{n.layer2.bias_mu.length()}, expected 3")
  }
}

///|
test "noisy_qnet_param_count" {
  let n = NoisyQNet::new(4, 8, 3, 1UL)
  // layer1: 32 weight + 8 bias × 2 = 80
  // layer2: 24 weight + 3 bias × 2 = 54
  // total: 134
  let count = noisy_qnet_param_count(n)
  if count != 134 {
    fail("param count = \{count}, expected 134")
  }
}

///|
test "noisy_q_forward_shape" {
  let n = NoisyQNet::new(4, 8, 3, 7UL)
  let x : Array[Float] = [1.0F, 0.5F, -0.5F, 0.2F]
  let q = noisy_q_forward(n, x, Xoshiro::new(123UL))
  if q.length() != 3 {
    fail("q.length() = \{q.length()}, expected 3")
  }
}

///|
test "noisy_q_forward_eval_shape" {
  let n = NoisyQNet::new(4, 8, 3, 7UL)
  let x : Array[Float] = [1.0F, 0.5F, -0.5F, 0.2F]
  let q = noisy_q_forward_eval(n, x)
  if q.length() != 3 {
    fail("q.length() = \{q.length()}, expected 3")
  }
}

///|
test "noisy_q_forward_eval_with_zero_sigma" {
  // When σ = 0, the noisy eval forward must match a no-noise pass
  // exactly (Float equality).
  let n = NoisyQNet::new(3, 4, 2, 5UL)
  let x : Array[Float] = [0.7F, -0.3F, 0.1F]
  let q_eval = noisy_q_forward_eval(n, x)
  // Compare to a hand-rolled linear path with μ weights only.
  let layer1_h1 : Array[Float] = Array::make(4, 0.0F)
  for o in 0..<4 {
    let mut acc : Float = n.layer1.bias_mu[o]
    for i in 0..<3 {
      acc = acc + n.layer1.weight_mu[o * 3 + i] * x[i]
    }
    layer1_h1[o] = if acc > 0.0F { acc } else { 0.0F }
  }
  let q_manual : Array[Float] = Array::make(2, 0.0F)
  for o in 0..<2 {
    let mut acc : Float = n.layer2.bias_mu[o]
    for i in 0..<4 {
      acc = acc + n.layer2.weight_mu[o * 4 + i] * layer1_h1[i]
    }
    q_manual[o] = acc
  }
  if q_eval[0] != q_manual[0] || q_eval[1] != q_manual[1] {
    fail("eval forward ≠ μ-only manual pass")
  }
}

///|
test "noisy_q_argmax_eval_action_in_range" {
  let n = NoisyQNet::new(4, 8, 3, 11UL)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let a = noisy_q_argmax_eval(n, x)
  if a < 0 || a >= 3 {
    fail("argmax_eval returned \{a}, expected in [0, 3)")
  }
}

///|
test "noisy_q_argmax_action_in_range" {
  let n = NoisyQNet::new(4, 8, 3, 11UL)
  let x : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let a = noisy_q_argmax(n, x, Xoshiro::new(7UL))
  if a < 0 || a >= 3 {
    fail("argmax returned \{a}, expected in [0, 3)")
  }
}

///|
test "noisy_q_forward_two_seeds_differ" {
  // Different RNGs should produce different forward outputs.
  let n_a = NoisyQNet::new(4, 8, 3, 1UL)
  let n_b = NoisyQNet::new(4, 8, 3, 1UL)
  // Both nets have the same initial weights (same seed in `new`),
  // so any output difference must come from the forward-time RNG.
  let x : Array[Float] = [1.0F, 0.5F, -0.5F, 0.2F]
  let q_a = noisy_q_forward(n_a, x, Xoshiro::new(100UL))
  let q_b = noisy_q_forward(n_b, x, Xoshiro::new(200UL))
  let mut any_diff = false
  for i in 0..<q_a.length() {
    if (q_a[i] - q_b[i]).abs() > 1.0e-6F {
      any_diff = true
    }
  }
  if !any_diff {
    fail("different forward RNGs produced identical Q-values")
  }
}

///|
test "noisy_q_forward_same_seed_deterministic" {
  let n_a = NoisyQNet::new(4, 8, 3, 5UL)
  let n_b = NoisyQNet::new(4, 8, 3, 5UL)
  let x : Array[Float] = [0.3F, -0.7F, 0.6F, -0.1F]
  let q_a = noisy_q_forward(n_a, x, Xoshiro::new(99UL))
  let q_b = noisy_q_forward(n_b, x, Xoshiro::new(99UL))
  for i in 0..<q_a.length() {
    if q_a[i] != q_b[i] {
      fail("forward differs at \{i}: \{q_a[i]} vs \{q_b[i]}")
    }
  }
}