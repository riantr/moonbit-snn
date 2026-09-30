// noisy_dqn_test.mbt — tests for v0.43.2 NoisyDQN agent.

///|
fn make_grid(seed : Int) -> GridWorld {
  GridWorld::new(2, 2, seed)
}

///|
/// GridWorld is hard-coded to 4 cardinal actions (up/down/left/right).
fn grid_n_actions() -> Int {
  4
}

///|
test "noisy_dqn_select_action_in_range" {
  let online = NoisyQNet::new(4, 8, 3, 1UL)
  let target = NoisyQNet::new(4, 8, 3, 2UL)
  let rng = Xoshiro::new(7UL)
  let a = noisy_dqn_select_action(online, 0, rng)
  if a < 0 || a >= 3 {
    fail("action \{a} out of [0, 3)")
  }
  let _ = target
}

///|
test "noisy_dqn_select_action_eval_in_range" {
  let online = NoisyQNet::new(4, 8, 3, 1UL)
  let a = noisy_dqn_select_action_eval(online, 1)
  if a < 0 || a >= 3 {
    fail("action \{a} out of [0, 3)")
  }
}

///|
test "noisy_qnet_copy_copies_all_params" {
  let src = NoisyQNet::new(3, 4, 2, 11UL)
  let dst = NoisyQNet::new(3, 4, 2, 22UL)
  // Mutate src weights so we can detect copy.
  src.layer1.weight_mu[0] = 99.0F
  src.layer1.bias_mu[0] = 88.0F
  src.layer1.weight_sigma[0] = 0.5F
  src.layer1.bias_sigma[0] = 0.4F
  src.layer2.weight_mu[5] = 77.0F
  src.layer2.bias_mu[1] = 66.0F
  src.layer2.weight_sigma[5] = 0.3F
  src.layer2.bias_sigma[1] = 0.2F
  noisy_qnet_copy(dst, src)
  if dst.layer1.weight_mu[0] != 99.0F {
    fail("dst.layer1.weight_mu[0] = \{dst.layer1.weight_mu[0]}, expected 99.0")
  }
  if dst.layer1.bias_mu[0] != 88.0F {
    fail("dst.layer1.bias_mu[0] not copied")
  }
  if dst.layer1.weight_sigma[0] != 0.5F {
    fail("dst.layer1.weight_sigma[0] = \{dst.layer1.weight_sigma[0]}")
  }
  if dst.layer1.bias_sigma[0] != 0.4F {
    fail("dst.layer1.bias_sigma[0] not copied")
  }
  if dst.layer2.weight_mu[5] != 77.0F {
    fail("dst.layer2.weight_mu[5] = \{dst.layer2.weight_mu[5]}")
  }
  if dst.layer2.bias_mu[1] != 66.0F {
    fail("dst.layer2.bias_mu[1] not copied")
  }
  if dst.layer2.weight_sigma[5] != 0.3F {
    fail("dst.layer2.weight_sigma[5] = \{dst.layer2.weight_sigma[5]}")
  }
  if dst.layer2.bias_sigma[1] != 0.2F {
    fail("dst.layer2.bias_sigma[1] not copied")
  }
}

///|
test "noisy_dqn_update_step_returns_mean_td" {
  // Run a single update step with a small batch; verify the
  // returned mean |δ| is finite and non-negative.
  let online = NoisyQNet::new(4, 4, 2, 5UL)
  let target = NoisyQNet::new(4, 4, 2, 6UL)
  let states : Array[Int] = [0, 1, 2, 3]
  let actions : Array[Int] = [0, 1, 1, 0]
  let rewards : Array[Float] = [1.0F, 0.0F, 1.0F, 0.5F]
  let next_states : Array[Int] = [1, 2, 3, 0]
  let dones : Array[Bool] = [false, false, false, true]
  let mean_td = noisy_dqn_update_step(
    online, target, states, actions, rewards, next_states, dones, 0.9F, 0.01F,
    Xoshiro::new(99UL),
  )
  if mean_td < 0.0F {
    fail("mean_td = \{mean_td}, expected ≥ 0")
  }
  if mean_td != mean_td {
    fail("mean_td is NaN")
  }
}

///|
test "noisy_dqn_update_step_changes_weights" {
  // Capture weights before, run one update, verify weights changed.
  let online = NoisyQNet::new(4, 4, 2, 5UL)
  let target = NoisyQNet::new(4, 4, 2, 6UL)
  let w0_before = online.layer1.weight_mu[0]
  let b0_before = online.layer2.bias_mu[0]
  let s_before = online.layer1.weight_sigma[0]
  let sb_before = online.layer2.bias_sigma[0]
  let states : Array[Int] = [0, 1, 2, 3]
  let actions : Array[Int] = [0, 1, 1, 0]
  let rewards : Array[Float] = [1.0F, 0.0F, 1.0F, 0.5F]
  let next_states : Array[Int] = [1, 2, 3, 0]
  let dones : Array[Bool] = [false, false, false, true]
  let _ = noisy_dqn_update_step(
    online, target, states, actions, rewards, next_states, dones, 0.9F, 0.1F,
    Xoshiro::new(99UL),
  )
  if online.layer1.weight_mu[0] == w0_before {
    fail("layer1.weight_mu[0] unchanged after update")
  }
  if online.layer2.bias_mu[0] == b0_before {
    fail("layer2.bias_mu[0] unchanged after update")
  }
  // σ should also have moved (paper §3.4).
  if online.layer1.weight_sigma[0] == s_before {
    fail("layer1.weight_sigma[0] unchanged after update")
  }
  if online.layer2.bias_sigma[0] == sb_before {
    fail("layer2.bias_sigma[0] unchanged after update")
  }
}

///|
test "train_noisy_dqn_runs_and_returns_mean_return" {
  // Smoke test: train for a few episodes and verify mean return is
  // finite (no NaN, no crash). Don't assert any specific value —
  // the gradient approximations mean training isn't yet reliable
  // for arbitrary envs.
  let env = make_grid(5)
  let target = NoisyQNet::new(env.n_states(), 8, grid_n_actions(), 7UL)
  let online = NoisyQNet::new(env.n_states(), 8, grid_n_actions(), 13UL)
  let mean_ret = train_noisy_dqn(
    env, online, target, 5, 0.9F, 0.01F, 20, 100, 1, 2, 4, 42UL,
  )
  if mean_ret != mean_ret {
    fail("mean_ret is NaN")
  }
  // Should be a finite float (within plausible range for env returns).
  if mean_ret < -100.0F || mean_ret > 100.0F {
    fail("mean_ret = \{mean_ret}, expected plausible range")
  }
}

///|
test "eval_noisy_dqn_runs_no_noise" {
  // Eval mode uses μ weights — same seed → deterministic.
  let env = make_grid(5)
  let online = NoisyQNet::new(env.n_states(), 8, grid_n_actions(), 13UL)
  // Pre-train briefly so the policy isn't totally random.
  let target = NoisyQNet::new(env.n_states(), 8, grid_n_actions(), 7UL)
  let _ = train_noisy_dqn(
    env, online, target, 3, 0.9F, 0.01F, 10, 50, 1, 1, 2, 42UL,
  )
  let ret = eval_noisy_dqn(env, online, 3, 10)
  if ret != ret {
    fail("eval return is NaN")
  }
}