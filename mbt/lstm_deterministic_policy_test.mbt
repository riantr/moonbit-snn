// lstm_deterministic_policy_test.mbt — Round-trip tests for the
// recurrent deterministic actor (v0.61.0).
//
// Coverage:
//   * constructor shape + hidden/cell dim match
//   * MLP hidden dim equals LSTM hidden dim (matched projection)
//   * action stays in [action_low, action_high] (tanh squash)
//   * single-step returns new hidden + new cell (length lstm_hidden)
//   * sequence forward threads hidden + cell through seq_len steps
//   * step at t=0 with zero states gives same result as seq_forward
//   * different seeds produce different actions

///|
test "lstm_deterministic_policy_new_dimensions" {
  let p : LSTMDeterministicPolicy = LSTMDeterministicPolicy::new(
    3, 2, 4, -1.0F, 1.0F, 42UL,
  )
  assert_eq(p.state_dim, 3)
  assert_eq(p.action_dim, 2)
  assert_eq(p.lstm_hidden, 4)
  // mlp_w1: lstm_hidden × state_dim
  assert_eq(p.mlp_w1.length(), 4)
  assert_eq(p.mlp_w1[0].length(), 3)
  // mlp_b1: lstm_hidden
  assert_eq(p.mlp_b1.length(), 4)
  // mlp_w2: action_dim × lstm_hidden
  assert_eq(p.mlp_w2.length(), 2)
  assert_eq(p.mlp_w2[0].length(), 4)
  // mlp_b2: action_dim
  assert_eq(p.mlp_b2.length(), 2)
  // LSTM cell: 4 gate matrices each d_h × (d_h + d_x) = 4 × (4 + 4) = 4×8
  assert_eq(p.lstm.d_x, 4)
  assert_eq(p.lstm.d_h, 4)
  assert_eq(p.lstm.w_f.length(), 4)
  assert_eq(p.lstm.w_f[0].length(), 8)
  assert_eq(p.lstm.w_i[0].length(), 8)
  assert_eq(p.lstm.w_c[0].length(), 8)
  assert_eq(p.lstm.w_o[0].length(), 8)
  assert_eq(p.lstm.b_f.length(), 4)
  assert_eq(p.lstm.b_i.length(), 4)
  assert_eq(p.lstm.b_c.length(), 4)
  assert_eq(p.lstm.b_o.length(), 4)
}

///|
test "lstm_deterministic_policy_action_in_range" {
  let p : LSTMDeterministicPolicy = LSTMDeterministicPolicy::new(
    5, 3, 8, -2.0F, 2.0F, 7UL,
  )
  let hidden : Array[Float] = Array::make(p.lstm_hidden, 0.0F)
  let cell : Array[Float] = Array::make(p.lstm_hidden, 0.0F)
  let obs : Array[Float] = [0.3F, -0.1F, 0.5F, 0.0F, 0.2F]
  let (action, new_hidden, new_cell) = lstm_deterministic_policy_step(
    p, obs, hidden, cell,
  )
  assert_eq(action.length(), 3)
  // tanh squash: action ∈ [action_low, action_high]
  for i in 0..<action.length() {
    assert_true(action[i] >= -2.0F - 1.0e-4F)
    assert_true(action[i] <= 2.0F + 1.0e-4F)
  }
  // new hidden + cell dim match
  assert_eq(new_hidden.length(), p.lstm_hidden)
  assert_eq(new_cell.length(), p.lstm_hidden)
}

///|
test "lstm_deterministic_policy_zero_initial_reproducible_under_seed" {
  let p1 : LSTMDeterministicPolicy = LSTMDeterministicPolicy::new(
    4, 2, 4, -1.0F, 1.0F, 99UL,
  )
  let p2 : LSTMDeterministicPolicy = LSTMDeterministicPolicy::new(
    4, 2, 4, -1.0F, 1.0F, 99UL,
  )
  let obs_seq : Array[Float] = [
    0.1F, 0.2F, 0.3F, 0.4F,
    0.5F, 0.6F, 0.7F, 0.8F,
    -0.1F, -0.2F, -0.3F, -0.4F,
  ]
  let hidden_init : Array[Float] = Array::make(p1.lstm_hidden, 0.0F)
  let cell_init : Array[Float] = Array::make(p1.lstm_hidden, 0.0F)
  let (act1, h1, c1) = lstm_deterministic_policy_seq_forward(
    p1, obs_seq, 3, hidden_init, cell_init,
  )
  let (act2, h2, c2) = lstm_deterministic_policy_seq_forward(
    p2, obs_seq, 3, hidden_init, cell_init,
  )
  // Element-wise equality.
  for i in 0..<act1.length() {
    assert_eq(act1[i], act2[i])
  }
  for i in 0..<h1.length() {
    assert_eq(h1[i], h2[i])
    assert_eq(c1[i], c2[i])
  }
}

///|
test "lstm_deterministic_policy_different_seeds_differ" {
  let p1 : LSTMDeterministicPolicy = LSTMDeterministicPolicy::new(
    4, 2, 4, -1.0F, 1.0F, 1UL,
  )
  let p2 : LSTMDeterministicPolicy = LSTMDeterministicPolicy::new(
    4, 2, 4, -1.0F, 1.0F, 2UL,
  )
  let obs : Array[Float] = [0.1F, 0.5F, -0.3F, 0.7F]
  let hidden_init : Array[Float] = Array::make(4, 0.0F)
  let cell_init : Array[Float] = Array::make(4, 0.0F)
  let (act1, _, _) = lstm_deterministic_policy_step(
    p1, obs, hidden_init, cell_init,
  )
  let (act2, _, _) = lstm_deterministic_policy_step(
    p2, obs, hidden_init, cell_init,
  )
  // At least one action element should differ between the two seeds.
  let mut differs = false
  for i in 0..<act1.length() {
    if act1[i] != act2[i] {
      differs = true
    }
  }
  assert_true(differs)
}

///|
test "lstm_deterministic_policy_seq_forward_threads_hidden_and_cell" {
  let p : LSTMDeterministicPolicy = LSTMDeterministicPolicy::new(
    2, 1, 3, -0.5F, 0.5F, 13UL,
  )
  let hidden_init : Array[Float] = Array::make(p.lstm_hidden, 0.0F)
  let cell_init : Array[Float] = Array::make(p.lstm_hidden, 0.0F)
  // Manually step through 3 observations and compare against seq_forward.
  let (a_step1, h_step1, c_step1) = lstm_deterministic_policy_step(
    p, [0.1F, 0.2F], hidden_init, cell_init,
  )
  let (a_step2, h_step2, c_step2) = lstm_deterministic_policy_step(
    p, [0.3F, 0.4F], h_step1, c_step1,
  )
  let (a_step3, h_step3, c_step3) = lstm_deterministic_policy_step(
    p, [0.5F, 0.6F], h_step2, c_step2,
  )
  // Now run seq_forward.
  let obs_seq : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F]
  let (act_seq, h_final, c_final) = lstm_deterministic_policy_seq_forward(
    p, obs_seq, 3, hidden_init, cell_init,
  )
  // Step-wise action equality.
  assert_eq(act_seq[0], a_step1[0])
  assert_eq(act_seq[1], a_step2[0])
  assert_eq(act_seq[2], a_step3[0])
  // Final hidden + cell equality.
  for i in 0..<h_step3.length() {
    assert_eq(h_final[i], h_step3[i])
    assert_eq(c_final[i], c_step3[i])
  }
}

///|
test "lstm_deterministic_policy_action_range_with_asymmetric_bounds" {
  // Asymmetric bounds [0.0, 3.0] - actions should map into that range.
  let p : LSTMDeterministicPolicy = LSTMDeterministicPolicy::new(
    3, 4, 6, 0.0F, 3.0F, 21UL,
  )
  let hidden : Array[Float] = Array::make(p.lstm_hidden, 0.0F)
  let cell : Array[Float] = Array::make(p.lstm_hidden, 0.0F)
  let obs : Array[Float] = [1.0F, -2.0F, 0.5F]
  let (action, _, _) = lstm_deterministic_policy_step(
    p, obs, hidden, cell,
  )
  for i in 0..<action.length() {
    assert_true(action[i] >= -1.0e-4F)
    assert_true(action[i] <= 3.0F + 1.0e-4F)
  }
}