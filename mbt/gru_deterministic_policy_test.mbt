// gru_deterministic_policy_test.mbt — Round-trip tests for the
// recurrent deterministic policy used by DDPG_GRU (v0.58.0).
//
// Coverage:
//   * constructor shape (state_dim, action_dim, gru_hidden, action_low,
//     action_high, seed) populates all fields correctly
//   * step produces action in [action_low, action_high]
//   * step is deterministic with same seed + same hidden
//   * different seeds produce different actions
//   * different hidden states produce different actions
//   * seq_forward produces seq_len * action_dim outputs
//   * seq_forward with zero hidden_init matches single-step chain
//   * hidden state evolves across timesteps (stateful)

// ============================================================================
// Helper: zero-filled hidden init
// ============================================================================

///|
fn zeros_hidden(gru_hidden : Int) -> Array[Float] {
  let h : Array[Float] = Array::make(gru_hidden, 0.0F)
  h
}

// ============================================================================
// Tests
// ============================================================================

///|
test "gru_deterministic_policy_new: shape + squash range" {
  let p : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    3, 2, 4, -1.0F, 1.0F, 7UL,
  )
  assert_eq(p.state_dim, 3)
  assert_eq(p.action_dim, 2)
  assert_eq(p.gru_hidden, 4)
  assert_eq(p.mlp_w1.length(), 4)
  assert_eq(p.mlp_w1[0].length(), 3)
  assert_eq(p.mlp_b1.length(), 4)
  assert_eq(p.mlp_w2.length(), 2)
  assert_eq(p.mlp_w2[0].length(), 4)
  assert_eq(p.mlp_b2.length(), 2)
  assert_eq(p.action_low, -1.0F)
  assert_eq(p.action_high, 1.0F)
}

///|
test "gru_deterministic_policy_step: action in squash range" {
  let p : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    3, 2, 4, -2.0F, 3.0F, 11UL,
  )
  let obs : Array[Float] = [0.5F, -0.3F, 0.1F]
  let h = zeros_hidden(4)
  let (action, _h_next) = gru_deterministic_policy_step(p, obs, h)
  assert_eq(action.length(), 2)
  assert_eq(action[0] >= -2.0F && action[0] <= 3.0F, true)
  assert_eq(action[1] >= -2.0F && action[1] <= 3.0F, true)
}

///|
test "gru_deterministic_policy_step: deterministic with same seed" {
  let p1 : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    3, 2, 4, -1.0F, 1.0F, 17UL,
  )
  let p2 : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    3, 2, 4, -1.0F, 1.0F, 17UL,
  )
  let obs : Array[Float] = [0.5F, -0.3F, 0.1F]
  let h = zeros_hidden(4)
  let (a1, _) = gru_deterministic_policy_step(p1, obs, h)
  let (a2, _) = gru_deterministic_policy_step(p2, obs, h)
  for i in 0..<a1.length() {
    assert_eq(a1[i], a2[i])
  }
}

///|
test "gru_deterministic_policy_step: different seeds differ" {
  let p1 : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    3, 2, 4, -1.0F, 1.0F, 17UL,
  )
  let p2 : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    3, 2, 4, -1.0F, 1.0F, 29UL,
  )
  let obs : Array[Float] = [0.5F, -0.3F, 0.1F]
  let h = zeros_hidden(4)
  let (a1, _) = gru_deterministic_policy_step(p1, obs, h)
  let (a2, _) = gru_deterministic_policy_step(p2, obs, h)
  let mut differs = false
  for i in 0..<a1.length() {
    if a1[i] != a2[i] {
      differs = true
    }
  }
  if not(differs) {
    abort("different seeds produced identical actions")
  }
}

///|
test "gru_deterministic_policy_step: hidden evolves over time" {
  // Two calls with the same observation but different hidden states
  // should produce different actions (because the GRU cell mixes the
  // previous hidden state into the new one).
  let p : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    3, 2, 4, -1.0F, 1.0F, 41UL,
  )
  let obs : Array[Float] = [0.5F, -0.3F, 0.1F]
  let h_zero = zeros_hidden(4)
  let h_other : Array[Float] = [0.7F, -0.4F, 0.2F, -0.1F]
  let (a_zero, _) = gru_deterministic_policy_step(p, obs, h_zero)
  let (a_other, _) = gru_deterministic_policy_step(p, obs, h_other)
  let mut differs = false
  for i in 0..<a_zero.length() {
    if a_zero[i] != a_other[i] {
      differs = true
    }
  }
  if not(differs) {
    abort("different hidden states produced identical actions")
  }
}

///|
test "gru_deterministic_policy_seq_forward: shape" {
  let p : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    2, 3, 5, -1.0F, 1.0F, 53UL,
  )
  let seq_len = 4
  let obs_seq : Array[Float] = Array::make(seq_len * 2, 0.0F)
  for i in 0..<obs_seq.length() {
    obs_seq[i] = Float::from_int(i) * 0.1F
  }
  let h = zeros_hidden(5)
  let (action_seq, hidden_out) = gru_deterministic_policy_seq_forward(p, obs_seq, seq_len, h)
  assert_eq(action_seq.length(), seq_len * 3)
  assert_eq(hidden_out.length(), 5)
  // All actions should be in the squash range.
  for i in 0..<action_seq.length() {
    assert_eq(action_seq[i] >= -1.0F && action_seq[i] <= 1.0F, true)
  }
}

///|
test "gru_deterministic_policy_seq_forward: matches single-step chain" {
  // Running seq_forward should produce the same action_seq as manually
  // chaining single-step calls.
  let p : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    2, 2, 3, -1.0F, 1.0F, 67UL,
  )
  let seq_len = 3
  let obs_seq : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F]
  let h0 = zeros_hidden(3)
  let (action_seq, _) = gru_deterministic_policy_seq_forward(p, obs_seq, seq_len, h0)
  // Manually chain.
  let mut h_cur = h0
  let mut all_match = true
  for t in 0..<seq_len {
    let obs_t : Array[Float] = [obs_seq[t * 2], obs_seq[t * 2 + 1]]
    let (a_t, h_new) = gru_deterministic_policy_step(p, obs_t, h_cur)
    let act_off = t * 2
    for k in 0..<2 {
      if a_t[k] != action_seq[act_off + k] {
        all_match = false
      }
    }
    h_cur = h_new
  }
  if not(all_match) {
    abort("seq_forward output differs from manual step chain")
  }
}

///|
test "gru_deterministic_policy_seq_forward: deterministic with same seed" {
  let p1 : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    2, 2, 4, -1.0F, 1.0F, 89UL,
  )
  let p2 : GRUDeterministicPolicy = GRUDeterministicPolicy::new(
    2, 2, 4, -1.0F, 1.0F, 89UL,
  )
  let obs_seq : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F]
  let h = zeros_hidden(4)
  let (a1, _) = gru_deterministic_policy_seq_forward(p1, obs_seq, 3, h)
  let (a2, _) = gru_deterministic_policy_seq_forward(p2, obs_seq, 3, h)
  for i in 0..<a1.length() {
    assert_eq(a1[i], a2[i])
  }
}
