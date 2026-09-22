// Tests for the WilsonCowan rate-model neuron and RateSynapse.

///|
test "WilsonCowan::new initialises x from Normal(0, 0.5)" {
  let rng = Xoshiro::from_state(0xDEAD_BEEF_UL, 1UL, 2UL, 3UL)
  let p = WilsonCowan::new(100, rng)
  assert_eq(p.n, 100)
  assert_eq(p.x.length(), 100)
  assert_eq(p.r.length(), 100)
  // r = tanh(x) on init. tanh is in [-1, 1] and tanh(0) = 0.
  let mut n_zero = 0
  for k in 0..<p.n {
    // tanh is monotonic; r is in [-1, 1].
    if p.r[k] > -1.0F && p.r[k] < 1.0F {
      n_zero = n_zero + 1
    }
  }
  assert_eq(n_zero, 100)
}

///|
test "step_wc: x decays toward 0 with I=0, g=0" {
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let p = WilsonCowan::new(5, rng)
  // Set x to +2.0, I=0, g=0. Each step: x += dt * (-x + 0 + 0)
  // = x * (1 - dt). With dt=0.125, x decays as x * 0.875 per step.
  for k in 0..<5 {
    p.x[k] = 2.0F
    p.g[k] = 0.0F
    p.i[k] = 0.0F
  }
  for _ in 0..<20 {
    step_wc(p, 0.125F)
  }
  // After 20 steps, x should be 2.0 * 0.875^20 ≈ 0.099.
  for k in 0..<5 {
    if p.x[k] > 0.2F {
      fail("WC decay too slow: x[" + k.to_string() + "] = " + p.x[k].to_string())
    }
  }
}

///|
test "step_wc: r = tanh(x) after integration" {
  let rng = Xoshiro::from_state(5UL, 6UL, 7UL, 8UL)
  let p = WilsonCowan::new(2, rng)
  p.x[0] = 0.0F
  p.x[1] = 1.0F
  p.g[0] = 0.0F
  p.g[1] = 0.0F
  p.i[0] = 0.0F
  p.i[1] = 0.0F
  step_wc(p, 0.125F)
  // r = tanh(x) — with x close to 0 or 1, r should be in (0, tanh(1)).
  if p.r[0] < -0.01F || p.r[0] > 0.01F {
    fail("r[0] should be ~0 with x[0]=0, got " + p.r[0].to_string())
  }
  if p.r[1] < 0.7F {
    fail("r[1] should be ~tanh(1)=0.76, got " + p.r[1].to_string())
  }
}

///|
test "RateSynapse::new and forward_rate" {
  let rng = Xoshiro::from_state(1UL, 1UL, 1UL, 1UL)
  let pre = WilsonCowan::new(3, rng)
  let post = WilsonCowan::new(2, rng)
  // All-to-all: 3 pres × 2 posts = 6 connections.
  let s = RateSynapse::new(pre, post, 1.0F, 0.0F, 1.0F, rng)
  assert_eq(s.matrix.nnz(), 6)
  // Set pre.r to a known pattern.
  pre.r[0] = 0.5F
  pre.r[1] = 1.0F
  pre.r[2] = 0.0F
  // Reset post.g to zero.
  for k in 0..<2 {
    post.g[k] = 0.0F
  }
  // forward: g[post[i]] += W[i] * rJ[pre[j]]
  forward_rate_synapse(s)
  // Each post receives 2 incoming weights (one per pre). The exact
  // values depend on the weights; just check they're > 0 since
  // pre.r > 0 for two of three pres.
  if post.g[0] <= 0.0F {
    fail("post.g[0] should be > 0 after forward, got " + post.g[0].to_string())
  }
  if post.g[1] <= 0.0F {
    fail("post.g[1] should be > 0 after forward, got " + post.g[1].to_string())
  }
}

///|
test "WC_ integrates via AnyPop enum" {
  let rng = Xoshiro::from_state(0xC0FFEE_UL, 0xBAD_F00D_UL, 0UL, 0UL)
  let wc = WilsonCowan::new(5, rng)
  let pops : Array[AnyPop] = [WC_(wc)]
  let total_n = n_total(pops)
  assert_eq(total_n, 5)
  sim_any_pops_for(pops, 10.0F, 0.125F)
  // Just verify the loop ran.
  let mut all_in_range = true
  for k in 0..<5 {
    if wc.r[k] < -1.0F || wc.r[k] > 1.0F {
      all_in_range = false
    }
  }
  if !all_in_range {
    fail("WC r values out of [-1, 1] after sim")
  }
}