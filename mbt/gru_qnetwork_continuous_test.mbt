// gru_qnetwork_continuous_test.mbt — Round-trip tests for the
// recurrent continuous-action Q-network (v0.59.0).

///|
fn zeros_hidden_qnet(hidden : Int) -> Array[Float] {
  let h : Array[Float] = Array::make(hidden, 0.0F)
  h
}

///|
test "gru_qnetwork_continuous_new: shape" {
  let q : GRUQNetworkContinuous = GRUQNetworkContinuous::new(3, 2, 4, 7UL)
  assert_eq(q.state_dim, 3)
  assert_eq(q.action_dim, 2)
  assert_eq(q.hidden, 4)
  // mlp_w1: hidden × (state_dim + action_dim) = 4 × 5
  assert_eq(q.mlp_w1.length(), 4)
  assert_eq(q.mlp_w1[0].length(), 5)
  assert_eq(q.mlp_b1.length(), 4)
  // mlp_w2: 1 × hidden = 1 × 4
  assert_eq(q.mlp_w2.length(), 1)
  assert_eq(q.mlp_w2[0].length(), 4)
  assert_eq(q.mlp_b2, 0.0F)
}

///|
test "gru_qnetwork_continuous_step: deterministic with same seed" {
  let q1 : GRUQNetworkContinuous = GRUQNetworkContinuous::new(3, 2, 4, 7UL)
  let q2 : GRUQNetworkContinuous = GRUQNetworkContinuous::new(3, 2, 4, 7UL)
  let state : Array[Float] = [0.5F, -0.3F, 0.1F]
  let action : Array[Float] = [0.4F, -0.2F]
  let h = zeros_hidden_qnet(4)
  let (q1_v, _) = gru_qnetwork_continuous_step(q1, state, action, h)
  let (q2_v, _) = gru_qnetwork_continuous_step(q2, state, action, h)
  assert_eq(q1_v, q2_v)
}

///|
test "gru_qnetwork_continuous_step: different seeds differ" {
  let q1 : GRUQNetworkContinuous = GRUQNetworkContinuous::new(3, 2, 4, 7UL)
  let q2 : GRUQNetworkContinuous = GRUQNetworkContinuous::new(3, 2, 4, 13UL)
  let state : Array[Float] = [0.5F, -0.3F, 0.1F]
  let action : Array[Float] = [0.4F, -0.2F]
  let h = zeros_hidden_qnet(4)
  let (q1_v, _) = gru_qnetwork_continuous_step(q1, state, action, h)
  let (q2_v, _) = gru_qnetwork_continuous_step(q2, state, action, h)
  if q1_v == q2_v {
    abort("different seeds produced identical Q values")
  }
}

///|
test "gru_qnetwork_continuous_step: hidden state evolves" {
  // Same (state, action) but different hidden states should produce
  // different Q values (because the GRU cell mixes the previous hidden
  // state into the new one).
  let q : GRUQNetworkContinuous = GRUQNetworkContinuous::new(3, 2, 4, 19UL)
  let state : Array[Float] = [0.5F, -0.3F, 0.1F]
  let action : Array[Float] = [0.4F, -0.2F]
  let h_zero = zeros_hidden_qnet(4)
  let h_other : Array[Float] = [0.7F, -0.4F, 0.2F, -0.1F]
  let (q_zero, _) = gru_qnetwork_continuous_step(q, state, action, h_zero)
  let (q_other, _) = gru_qnetwork_continuous_step(q, state, action, h_other)
  if q_zero == q_other {
    abort("different hidden states produced identical Q values")
  }
}

///|
test "gru_qnetwork_continuous_step: zero state + action + hidden -> finite Q" {
  let q : GRUQNetworkContinuous = GRUQNetworkContinuous::new(3, 2, 4, 23UL)
  let state : Array[Float] = [0.0F, 0.0F, 0.0F]
  let action : Array[Float] = [0.0F, 0.0F]
  let h = zeros_hidden_qnet(4)
  let (q_v, _) = gru_qnetwork_continuous_step(q, state, action, h)
  // Q should be 0.0F (ReLU zero + sigmoid 0.5 + tanh 0 = 0).
  assert_eq(q_v, 0.0F)
}

///|
test "gru_qnetwork_continuous_seq_forward: shape + deterministic" {
  let q1 : GRUQNetworkContinuous = GRUQNetworkContinuous::new(2, 1, 3, 29UL)
  let q2 : GRUQNetworkContinuous = GRUQNetworkContinuous::new(2, 1, 3, 29UL)
  let seq_len = 4
  let state_seq : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F]
  let action_seq : Array[Float] = [0.5F, -0.5F, 0.3F, -0.3F]
  let h = zeros_hidden_qnet(3)
  let (q_seq_1, _) = gru_qnetwork_continuous_seq_forward(q1, state_seq, action_seq, seq_len, h)
  let (q_seq_2, _) = gru_qnetwork_continuous_seq_forward(q2, state_seq, action_seq, seq_len, h)
  assert_eq(q_seq_1.length(), seq_len)
  for i in 0..<seq_len {
    assert_eq(q_seq_1[i], q_seq_2[i])
  }
}

///|
test "gru_qnetwork_continuous_seq_forward: matches single-step chain" {
  let q : GRUQNetworkContinuous = GRUQNetworkContinuous::new(2, 1, 3, 41UL)
  let seq_len = 3
  let state_seq : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F]
  let action_seq : Array[Float] = [0.5F, -0.5F, 0.3F]
  let h0 = zeros_hidden_qnet(3)
  let (q_seq, _) = gru_qnetwork_continuous_seq_forward(q, state_seq, action_seq, seq_len, h0)
  // Manually chain.
  let mut h_cur = h0
  let mut all_match = true
  for t in 0..<seq_len {
    let st : Array[Float] = [state_seq[t * 2], state_seq[t * 2 + 1]]
    let at : Array[Float] = [action_seq[t]]
    let (q_t, h_new) = gru_qnetwork_continuous_step(q, st, at, h_cur)
    if q_t != q_seq[t] {
      all_match = false
    }
    h_cur = h_new
  }
  if not(all_match) {
    abort("seq_forward output differs from manual step chain")
  }
}

///|
test "gru_qnetwork_continuous_step: scalar return type" {
  // The step function returns Float, not Array[Float]. Verify by
  // assigning to a Float-typed binding.
  let q : GRUQNetworkContinuous = GRUQNetworkContinuous::new(3, 2, 4, 43UL)
  let state : Array[Float] = [0.5F, -0.3F, 0.1F]
  let action : Array[Float] = [0.4F, -0.2F]
  let h = zeros_hidden_qnet(4)
  let (q_v, hidden_next) = gru_qnetwork_continuous_step(q, state, action, h)
  // Type-level: q_v is Float (no length()).
  assert_eq(hidden_next.length(), 4)
  // Float is finite (not NaN).
  if q_v != q_v {
    abort("Q value is NaN")
  }
}
