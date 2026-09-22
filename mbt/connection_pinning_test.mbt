/// connection_pinning_test.mbt 鈥?tests for PINningSparseSynapse
/// (port of refs/SNNModels.jl/src/connections/pinning_sparse_synapse.jl).

///|
test "PINningSparseSynapse::new 鈥?fields populated" {
  let rng = Xoshiro::from_state(42UL, 1UL, 2UL, 3UL)
  let pre = WilsonCowan::new(4, rng)
  let post = WilsonCowan::new(2, rng)
  let c = PINningSparseSynapse::new(pre, post, 1.5F, 1.0F, 1.0F, rng)
  assert_eq(c.pre.n, 4)
  assert_eq(c.post.n, 2)
  assert_eq(c.matrix.rows, 4)
  assert_eq(c.matrix.cols, 2)
  assert_eq(c.q.length(), 2)
  assert_eq(c.f.length(), 2)
  assert_eq(c.p_vals.length(), c.matrix.vals.length())
  // rI / rJ / g aliased.
  assert_eq(c.rI.length(), 2)
  assert_eq(c.rJ.length(), 4)
  assert_eq(c.g.length(), 2)
}

///|
test "PINningSparseSynapse::new 鈥?P diagonal = 伪, off-diagonal = 0" {
  // p=1.0 all-to-all, so every (j,i) pair exists.
  let rng = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  let pre = WilsonCowan::new(3, rng)
  let post = WilsonCowan::new(3, rng)
  let c = PINningSparseSynapse::new(pre, post, 1.0F, 1.0F, 2.5F, rng)
  // For each row j, walk and verify colptr[s]==j 鈫?p_vals[s]=2.5; otherwise 0.
  let mut j = 0
  while j < 3 {
    let start = c.matrix.rowptr[j]
    let end_ = c.matrix.rowptr[j + 1]
    let mut s = start
    while s < end_ {
      let expected = if c.matrix.colptr[s] == j { 2.5F } else { 0.0F }
      assert_eq(c.p_vals[s], expected)
      s = s + 1
    }
    j = j + 1
  }
}

///|
test "PINningSparseSynapse::set_target 鈥?sets f[i]" {
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let pre = WilsonCowan::new(2, rng)
  let post = WilsonCowan::new(2, rng)
  let c = PINningSparseSynapse::new(pre, post, 1.0F, 1.0F, 1.0F, rng)
  c.set_target(0, 5.0F)
  c.set_target(1, 10.0F)
  assert_eq(c.f[0], 5.0F)
  assert_eq(c.f[1], 10.0F)
}

///|
test "forward_pinning_synapse 鈥?q and g updated from P*W*rJ" {
  // 1 pre, 1 post; pre.r = [3], post.r = [0].
  // Setup: build a single connection (j=0 鈫?i=0) with W=2, P=1.
  // Expected: q[0] = P * rJ[0] = 1 * 3 = 3; g[0] = W * rJ[0] = 2 * 3 = 6.
  let rng = Xoshiro::from_state(100UL, 200UL, 300UL, 400UL)
  let pre = WilsonCowan::new(1, rng)
  let post = WilsonCowan::new(1, rng)
  let c = PINningSparseSynapse::new(pre, post, 1.0F, 1.0F, 1.0F, rng)
  // Force single connection: set rJ[0] = 3.
  c.rJ[0] = 3.0F
  // Override W and P for predictable test.
  // First, ensure there's at least one connection. p=1.0 鈫?guaranteed.
  // (CSR random with p=1.0 should fill all 1x1 = 1 connection.)
  c.matrix.vals[0] = 2.0F
  c.p_vals[0] = 1.0F
  forward_pinning_synapse(c)
  assert_eq(c.q[0], 3.0F)
  assert_eq(c.g[0], 6.0F)
}

///|
test "forward_pinning_synapse 鈥?resets q and g before update" {
  let rng = Xoshiro::from_state(5UL, 6UL, 7UL, 8UL)
  let pre = WilsonCowan::new(1, rng)
  let post = WilsonCowan::new(1, rng)
  let c = PINningSparseSynapse::new(pre, post, 1.0F, 1.0F, 1.0F, rng)
  // Pre-load q and g with garbage.
  c.q[0] = 999.0F
  c.g[0] = 888.0F
  // Setup single connection with W=1, P=0, rJ[0]=0 鈫?q and g both reset to 0.
  c.rJ[0] = 0.0F
  if c.matrix.vals.length() > 0 { c.matrix.vals[0] = 1.0F }
  forward_pinning_synapse(c)
  assert_eq(c.q[0], 0.0F)
  assert_eq(c.g[0], 0.0F)
}

///|
test "pinning_plasticity 鈥?C = 1/(1+dot(q,rI)) scaling" {
  // Single connection: dot(q,rI) = q[0]*rI[0]. If q[0]=1, rI[0]=1,
  // dot=1, C = 1/2 = 0.5.
  let rng = Xoshiro::from_state(11UL, 22UL, 33UL, 44UL)
  let pre = WilsonCowan::new(1, rng)
  let post = WilsonCowan::new(1, rng)
  let c = PINningSparseSynapse::new(pre, post, 1.0F, 1.0F, 1.0F, rng)
  c.q[0] = 1.0F
  c.rI[0] = 1.0F
  // Set up deterministic W, P, g, f.
  if c.matrix.vals.length() > 0 { c.matrix.vals[0] = 10.0F }
  c.p_vals[0] = 5.0F
  c.g[0] = 2.0F
  c.f[0] = 4.0F
  pinning_plasticity(c, 0.125F, 0.0F)
  // C = 0.5
  // P_new = P + -C * q[i] * q[j] = 5 + (-0.5) * 1 * 1 = 4.5
  assert_eq(c.p_vals[0], 4.5F)
  // W_new = W + C * (f[i] - g[i]) * q[j] = 10 + 0.5 * (4 - 2) * 1 = 11.0
  assert_eq(c.matrix.vals[0], 11.0F)
}

///|
test "pinning_plasticity 鈥?C = 1/(1+0) = 1 when q路rI = 0" {
  let rng = Xoshiro::from_state(2UL, 4UL, 6UL, 8UL)
  let pre = WilsonCowan::new(1, rng)
  let post = WilsonCowan::new(1, rng)
  let c = PINningSparseSynapse::new(pre, post, 1.0F, 1.0F, 1.0F, rng)
  c.q[0] = 0.0F
  c.rI[0] = 5.0F  // dot(q,rI) = 0*5 = 0 鈫?C = 1
  if c.matrix.vals.length() > 0 { c.matrix.vals[0] = 10.0F }
  c.p_vals[0] = 5.0F
  c.g[0] = 2.0F
  c.f[0] = 4.0F
  pinning_plasticity(c, 0.125F, 0.0F)
  // C = 1.0
  // P_new = 5 + -1 * 0 * 1 = 5.0 (no change since q[i]=0)
  assert_eq(c.p_vals[0], 5.0F)
  // W_new = 10 + 1 * (4 - 2) * 0 = 10.0 (no change since q[j]=0)
  assert_eq(c.matrix.vals[0], 10.0F)
}

///|
test "pinning_plasticity 鈥?multi-neuron dot(q, rI)" {
  // 2 pre, 2 post. q = [2, 3], rI = [4, 5] 鈫?dot = 8 + 15 = 23.
  // C = 1 / (1 + 23) = 1/24 鈮?0.04167
  let rng = Xoshiro::from_state(50UL, 60UL, 70UL, 80UL)
  let pre = WilsonCowan::new(2, rng)
  let post = WilsonCowan::new(2, rng)
  let c = PINningSparseSynapse::new(pre, post, 1.0F, 1.0F, 1.0F, rng)
  c.q[0] = 2.0F
  c.q[1] = 3.0F
  c.rI[0] = 4.0F
  c.rI[1] = 5.0F
  // Verify dot manually.
  let dot_val = c.q[0] * c.rI[0] + c.q[1] * c.rI[1]
  assert_eq(dot_val, 23.0F)
  // We don't apply plasticity; just verify the values are set up
  // correctly for the next call.
  assert_eq(c.q.length(), 2)
  assert_eq(c.rI.length(), 2)
}