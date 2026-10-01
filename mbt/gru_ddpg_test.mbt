// gru_ddpg_test.mbt — Round-trip tests for the DDPG agent with
// GRU-based actor and twin GRU-based critics (v0.60.0).

///|
test "ddpg_gru_new: 6 networks built + hyperparameters stored" {
  let agent : DDPG_GRU = DDPG_GRU::new(
    3, 2, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 7UL,
  )
  assert_eq(agent.actor.state_dim, 3)
  assert_eq(agent.actor.action_dim, 2)
  assert_eq(agent.actor.gru_hidden, 4)
  assert_eq(agent.critic1.state_dim, 3)
  assert_eq(agent.critic1.action_dim, 2)
  assert_eq(agent.critic1.hidden, 4)
  assert_eq(agent.actor_target.action_low, -1.0F)
  assert_eq(agent.actor_target.action_high, 1.0F)
  assert_eq(agent.gamma, 0.99F)
  assert_eq(agent.tau, 0.005F)
  assert_eq(agent.exploration_noise, 0.1F)
  assert_eq(agent.hidden, 4)
}

///|
test "ddpg_gru_new: source and target networks start distinct" {
  // Polyak-averaged targets start at seed+100/101/102 to be far from
  // the source networks (seed/seed+1/seed+2). Verify that the actor's
  // initial weights differ from the actor_target's.
  let agent : DDPG_GRU = DDPG_GRU::new(
    3, 2, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 7UL,
  )
  let mut diff = false
  for i in 0..<agent.actor.mlp_w1.length() {
    for k in 0..<agent.actor.mlp_w1[i].length() {
      if agent.actor.mlp_w1[i][k] != agent.actor_target.mlp_w1[i][k] {
        diff = true
      }
    }
  }
  if not(diff) {
    abort("actor and actor_target started identical (Polyak soft-update would be a no-op)")
  }
}

///|
test "ddpg_gru_select_action: deterministic with noise_std=0" {
  let agent : DDPG_GRU = DDPG_GRU::new(
    3, 2, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 17UL,
  )
  let obs : Array[Float] = [0.5F, -0.3F, 0.1F]
  let h : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let (a1, _) = ddpg_gru_select_action(agent, obs, h, 0.0F, rng)
  let (a2, _) = ddpg_gru_select_action(agent, obs, h, 0.0F, rng)
  for i in 0..<a1.length() {
    assert_eq(a1[i], a2[i])
  }
}

///|
test "ddpg_gru_select_action: noise clipped to action range" {
  let agent : DDPG_GRU = DDPG_GRU::new(
    3, 2, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 23UL,
  )
  let obs : Array[Float] = [0.5F, -0.3F, 0.1F]
  let h : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  let rng = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  let (action, _) = ddpg_gru_select_action(agent, obs, h, 5.0F, rng)
  // noise_std=5 is far larger than the [-1, 1] range; the result must
  // be clipped.
  assert_eq(action[0] >= -1.0F && action[0] <= 1.0F, true)
  assert_eq(action[1] >= -1.0F && action[1] <= 1.0F, true)
}

///|
test "ddpg_gru_select_action: hidden state evolves" {
  let agent : DDPG_GRU = DDPG_GRU::new(
    3, 2, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 29UL,
  )
  let obs : Array[Float] = [0.5F, -0.3F, 0.1F]
  let h_zero : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]
  let h_other : Array[Float] = [0.7F, -0.4F, 0.2F, -0.1F]
  let rng = Xoshiro::from_state(31UL, 37UL, 41UL, 43UL)
  let (a1, _) = ddpg_gru_select_action(agent, obs, h_zero, 0.0F, rng)
  let (a2, _) = ddpg_gru_select_action(agent, obs, h_other, 0.0F, rng)
  let mut diff = false
  for i in 0..<a1.length() {
    if a1[i] != a2[i] {
      diff = true
    }
  }
  if not(diff) {
    abort("different hidden states produced identical actions")
  }
}

///|
test "ddpg_gru_compute_td_target_seq: shape + finiteness" {
  let agent : DDPG_GRU = DDPG_GRU::new(
    2, 1, 3, -1.0F, 1.0F, 0.9F, 0.01F, 0.05F, 37UL,
  )
  let seq_len = 3
  let next_obs_seq : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F]
  let next_act_seq : Array[Float] = [0.5F, -0.5F, 0.3F]
  let reward_seq : Array[Float] = [1.0F, 0.5F, -0.2F]
  let done_seq : Array[Float] = [0.0F, 0.0F, 1.0F]
  let target = ddpg_gru_compute_td_target_seq(agent, next_obs_seq, next_act_seq, reward_seq, done_seq, seq_len)
  assert_eq(target.length(), seq_len)
  for t in 0..<seq_len {
    // Q value should be finite (not NaN).
    if target[t] != target[t] {
      abort("TD target is NaN at t=" + t.to_string())
    }
  }
}

///|
test "ddpg_gru_compute_td_target_seq: zero rewards + gamma=0 -> zero target" {
  // With gamma=0 and rewards=0, the TD target is just 0 regardless
  // of critic values. Useful as a smoke test that the formula is
  // wired correctly.
  let agent : DDPG_GRU = DDPG_GRU::new(
    2, 1, 3, -1.0F, 1.0F, 0.0F, 0.01F, 0.05F, 41UL,
  )
  let seq_len = 3
  let next_obs_seq : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F]
  let next_act_seq : Array[Float] = [0.5F, -0.5F, 0.3F]
  let reward_seq : Array[Float] = [0.0F, 0.0F, 0.0F]
  let done_seq : Array[Float] = [0.0F, 0.0F, 0.0F]
  let target = ddpg_gru_compute_td_target_seq(agent, next_obs_seq, next_act_seq, reward_seq, done_seq, seq_len)
  for t in 0..<seq_len {
    assert_eq(target[t], 0.0F)
  }
}

///|
test "ddpg_gru_soft_update: actor_target moves toward actor" {
  // After one soft-update with tau=1.0, the actor_target should
  // exactly match the actor (Polyak averaging with tau=1 means full
  // copy).
  let agent : DDPG_GRU = DDPG_GRU::new(
    3, 2, 4, -1.0F, 1.0F, 0.99F, 0.005F, 0.1F, 47UL,
  )
  let agent_tau1 : DDPG_GRU = { ..agent, tau: 1.0F }
  ddpg_gru_soft_update(agent_tau1)
  for i in 0..<agent_tau1.actor.mlp_w1.length() {
    for k in 0..<agent_tau1.actor.mlp_w1[i].length() {
      assert_eq(agent_tau1.actor.mlp_w1[i][k], agent_tau1.actor_target.mlp_w1[i][k])
    }
  }
  // Check GRU sub-parameters too.
  for i in 0..<agent_tau1.actor.gru.w_z.length() {
    for k in 0..<agent_tau1.actor.gru.w_z[i].length() {
      assert_eq(agent_tau1.actor.gru.w_z[i][k], agent_tau1.actor_target.gru.w_z[i][k])
      assert_eq(agent_tau1.actor.gru.w_r[i][k], agent_tau1.actor_target.gru.w_r[i][k])
      assert_eq(agent_tau1.actor.gru.w_n[i][k], agent_tau1.actor_target.gru.w_n[i][k])
    }
  }
}

///|
test "ddpg_gru_soft_update: tau=0 leaves target unchanged" {
  let agent : DDPG_GRU = DDPG_GRU::new(
    3, 2, 4, -1.0F, 1.0F, 0.99F, 0.0F, 0.1F, 53UL,
  )
  // Snapshot the actor_target before soft update.
  let w00_before = agent.actor_target.mlp_w1[0][0]
  ddpg_gru_soft_update(agent)
  let w00_after = agent.actor_target.mlp_w1[0][0]
  assert_eq(w00_before, w00_after)
}

///|
test "ddpg_gru_soft_update: critics are also soft-updated" {
  let agent : DDPG_GRU = DDPG_GRU::new(
    3, 2, 4, -1.0F, 1.0F, 0.99F, 1.0F, 0.1F, 59UL,
  )
  let agent_tau1 : DDPG_GRU = { ..agent, tau: 1.0F }
  ddpg_gru_soft_update(agent_tau1)
  // critic1_target should now match critic1.
  for i in 0..<agent_tau1.critic1.mlp_w1.length() {
    for k in 0..<agent_tau1.critic1.mlp_w1[i].length() {
      assert_eq(agent_tau1.critic1.mlp_w1[i][k], agent_tau1.critic1_target.mlp_w1[i][k])
    }
  }
  assert_eq(agent_tau1.critic1.mlp_b2, agent_tau1.critic1_target.mlp_b2)
  // critic2_target should now match critic2.
  assert_eq(agent_tau1.critic2.mlp_b2, agent_tau1.critic2_target.mlp_b2)
}
