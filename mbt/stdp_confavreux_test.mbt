// Tests for STDPConfavreux2025 (Confavreux 2025 STDP variant with
// alpha / beta baseline rate dependencies).
//
// Mirrors SpikingNeuralNetworks.jl/test/syn/with_plasticity.jl
// ("STDPConfavreux2025" testset) 鈥?the Julia test only asserts that
// weights remain finite after `train!`; we replicate that plus a few
// directional / clamping tests to exercise the rule body.

///|
test "STDPConfavreux2025::new: defaults match Julia" {
  let p = STDPConfavreux2025::new()
  if p.eta != 0.01F {
    fail("eta = \{p.eta}, expected 0.01")
  }
  if p.alpha != 0.0F {
    fail("alpha = \{p.alpha}, expected 0.0")
  }
  if p.beta != 0.0F {
    fail("beta = \{p.beta}, expected 0.0")
  }
  if p.kappa != 1.0F {
    fail("kappa = \{p.kappa}, expected 1.0")
  }
  if p.gamma != 1.0F {
    fail("gamma = \{p.gamma}, expected 1.0")
  }
  if p.tau_pre != 20.0F {
    fail("tau_pre = \{p.tau_pre}, expected 20.0")
  }
  if p.tau_post != 20.0F {
    fail("tau_post = \{p.tau_post}, expected 20.0")
  }
  if p.w_max != 30.0F {
    fail("w_max = \{p.w_max}, expected 30.0")
  }
  if p.w_min != 0.0F {
    fail("w_min = \{p.w_min}, expected 0.0")
  }
}

///|
test "STDPEntryConfavreux2025::new: vars zero-initialised" {
  let e = STDPEntryConfavreux2025::new(0, 5, 4)
  if e.conn_index != 0 {
    fail("conn_index = \{e.conn_index}, expected 0")
  }
  if e.n_pre != 5 || e.n_post != 4 {
    fail("n_pre/n_post = \{e.n_pre}/\{e.n_post}, expected 5/4")
  }
  if e.vars.tpre.length() != 5 || e.vars.tpost.length() != 4 {
    fail("trace lengths wrong")
  }
  for k in 0..<5 {
    if e.vars.tpre[k] != 0.0F {
      fail("tpre[\{k}] should be 0 initially, got \{e.vars.tpre[k]}")
    }
    if e.vars.last_pre[k] != 0.0F {
      fail("last_pre[\{k}] should be 0 initially")
    }
  }
  for k in 0..<4 {
    if e.vars.tpost[k] != 0.0F {
      fail("tpost[\{k}] should be 0 initially")
    }
  }
  if e.t_now[0] != 0.0F {
    fail("t_now should be 0 initially")
  }
}

///|
test "STDPEntryConfavreux2025::change_plasticity: swaps param, preserves vars" {
  let e = STDPEntryConfavreux2025::new(0, 3, 3)
  e.vars.tpre[0] = 0.7F
  e.vars.tpost[1] = 0.5F
  let new_param : STDPConfavreux2025 = {
    eta: 0.05F,
    alpha: 0.1F,
    beta: 0.2F,
    kappa: 2.0F,
    gamma: 0.5F,
    tau_pre: 30.0F,
    tau_post: 40.0F,
    w_max: 100.0F,
    w_min: -10.0F,
  }
  e.change_plasticity(new_param)
  if e.param.eta != 0.05F {
    fail("after swap, eta = \{e.param.eta}, expected 0.05")
  }
  // Vars should be preserved.
  if e.vars.tpre[0] != 0.7F {
    fail("vars.tpre[0] should be preserved, got \{e.vars.tpre[0]}")
  }
  if e.vars.tpost[1] != 0.5F {
    fail("vars.tpost[1] should be preserved")
  }
}

///|
test "stdp_confavreux_step: pre spike adds kappa * tpost[i] + alpha" {
  // 1 pre neuron 鈫?1 post neuron, fully connected.
  // Set up: alpha=0.1, kappa=1.0; tpost[0]=0.5.
  // Step: pre fires, post does not.
  // Expected: w[0] += eta * (kappa * tpost[0] + alpha) = 0.01 * (1*0.5 + 0.1) = 0.006.
  let w : Array[Float] = [0.5F]
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [false]
  let colptr : Array[Int] = [0] // post index of connection s=0
  let rowptr : Array[Int] = [0, 1] // pre 0 has 1 connection starting at 0
  let vars = STDPVariables::new(1, 1)
  // Force tpost[0] = 0.5 (bypass decay by starting at 0.5).
  vars.tpost[0] = 0.5F
  let p : STDPConfavreux2025 = {
    eta: 0.01F,
    alpha: 0.1F,
    beta: 0.0F,
    kappa: 1.0F,
    gamma: 1.0F,
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_max: 30.0F,
    w_min: 0.0F,
  }
  let dt = 0.125F
  stdp_confavreux_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, 0.0F, dt)
  // tpost[0] was decayed before the weight update by `decay_post = exp(-dt/tau_post)`.
  // exp(-0.125/20) 鈮?0.99376. So tpost[0] after decay 鈮?0.5 * 0.99376 鈮?0.49688.
  // 螖w = 0.01 * (1 * 0.49688 + 0.1) = 0.01 * 0.59688 鈮?0.0059688.
  let expected = 0.01F * (0.5F * expf(-dt / 20.0F) + 0.1F)
  if (w[0] - (0.5F + expected)).abs() > 1.0e-5F {
    fail("w[0] = \{w[0]}, expected ~\{0.5F + expected}")
  }
}

///|
test "stdp_confavreux_step: post spike adds gamma * tpre[j] + beta" {
  // 1 pre 鈫?1 post. post fires, pre does not.
  // Force tpre[0] = 0.7. Expected: w[0] += eta * (gamma * 0.7 * decay_pre + beta)
  // = 0.01 * (1 * 0.7 * exp(-0.125/20) + 0.2) 鈮?0.01 * (0.6956 + 0.2) = 0.008956.
  let w : Array[Float] = [0.3F]
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [true]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = STDPVariables::new(1, 1)
  vars.tpre[0] = 0.7F
  let p : STDPConfavreux2025 = {
    eta: 0.01F,
    alpha: 0.0F,
    beta: 0.2F,
    kappa: 1.0F,
    gamma: 1.0F,
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_max: 30.0F,
    w_min: 0.0F,
  }
  let dt = 0.125F
  stdp_confavreux_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, 0.0F, dt)
  let expected = 0.01F * (1.0F * 0.7F * expf(-dt / 20.0F) + 0.2F)
  if (w[0] - (0.3F + expected)).abs() > 1.0e-5F {
    fail("w[0] = \{w[0]}, expected ~\{0.3F + expected}")
  }
}

///|
test "stdp_confavreux_step: pre + post both fire same step 鈥?additive" {
  // Both fire 鈫?both contributions apply, alpha + beta baselines included.
  // After the spike update, tpre and tpost are bumped, but the weight
  // update uses the post-bump values (since the spike bump is done
  // before the connection loop).
  let w : Array[Float] = [0.1F]
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [true]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = STDPVariables::new(1, 1)
  // Start with non-zero traces.
  vars.tpre[0] = 0.3F
  vars.tpost[0] = 0.4F
  let p : STDPConfavreux2025 = {
    eta: 0.1F,
    alpha: 0.05F,
    beta: 0.07F,
    kappa: 2.0F,
    gamma: 0.5F,
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_max: 30.0F,
    w_min: 0.0F,
  }
  let dt = 0.125F
  stdp_confavreux_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, 0.0F, dt)
  // tpre after step: 0.3 * decay_pre + 1.0 (spike bump)
  // tpost after step: 0.4 * decay_post + 1.0
  let decay_pre : Float = expf(-dt / 20.0F)
  let decay_post : Float = expf(-dt / 20.0F)
  let tpre_after = 0.3F * decay_pre + 1.0F
  let tpost_after = 0.4F * decay_post + 1.0F
  // Both spikes apply on this connection (it's the only one, j=0, post_idx=0).
  let dw_pre = 0.1F * (2.0F * tpost_after + 0.05F)
  let dw_post = 0.1F * (0.5F * tpre_after + 0.07F)
  let expected = 0.1F + dw_pre + dw_post
  if (w[0] - expected).abs() > 1.0e-5F {
    fail("w[0] = \{w[0]}, expected ~\{expected}")
  }
}

///|
test "stdp_confavreux_step: weight clamps at w_max (LTP side)" {
  // Repeated post firing drives weight toward w_max.
  let w : Array[Float] = [29.5F] // start near w_max
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [true]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = STDPVariables::new(1, 1)
  vars.tpre[0] = 1.0F
  let p : STDPConfavreux2025 = {
    eta: 0.01F,
    alpha: 0.0F,
    beta: 0.0F,
    kappa: 1.0F,
    gamma: 1.0F,
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_max: 30.0F,
    w_min: 0.0F,
  }
  let dt = 0.125F
  // Run many iterations with post firing each step.
  for _ in 0..<2000 {
    stdp_confavreux_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, 0.0F, dt)
    // Reset trace each step so the bump is constant.
    vars.tpre[0] = 1.0F
  }
  // Weight should be clamped at w_max.
  if (w[0] - 30.0F).abs() > 1.0e-5F {
    fail("w[0] = \{w[0]}, expected clamp at w_max=30.0")
  }
}

///|
test "stdp_confavreux_step: weights stay finite over many steps" {
  // Sanity check: drive with both pre and post firing every step; ensure
  // no NaN / Inf emerges (matches Julia's `all(isfinite, syn.W)` test).
  let n_pre = 4
  let n_post = 4
  let n_conn = 16
  let w : Array[Float] = Array::make(n_conn, 1.0F)
  let pre_fire : Array[Bool] = Array::make(n_pre, true)
  let post_fire : Array[Bool] = Array::make(n_post, true)
  // Dense 4x4: rowptr = [0, 4, 8, 12, 16]; colptr = [0,1,2,3,0,1,2,3,...].
  let rowptr : Array[Int] = [0, 4, 8, 12, 16]
  let colptr : Array[Int] = [
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
  ]
  let vars = STDPVariables::new(n_pre, n_post)
  let p = STDPConfavreux2025::new()
  let dt = 0.125F
  for step in 0..<200 {
    let _ = step
    stdp_confavreux_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, Float::from_int(step) * dt, dt)
  }
  for s in 0..<n_conn {
    if (w[s]).is_nan() || (w[s]).is_inf() {
      fail("w[\{s}] = \{w[s]} is not finite")
    }
  }
}

///|
test "stdp_confavreux_step: integration 鈥?random pre/post spikes" {
  // Mixed pre/post firing across a 4脳4 dense synapse for 100 steps.
  // After clamping, weights should remain finite and bounded.
  let n_pre = 4
  let n_post = 4
  let n_conn = 16
  let w : Array[Float] = Array::make(n_conn, 1.0F)
  let pre_fire : Array[Bool] = Array::make(n_pre, false)
  let post_fire : Array[Bool] = Array::make(n_post, false)
  let rowptr : Array[Int] = [0, 4, 8, 12, 16]
  let colptr : Array[Int] = [
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
  ]
  let vars = STDPVariables::new(n_pre, n_post)
  let p = STDPConfavreux2025::new()
  let dt = 0.125F
  let rng = Xoshiro::from_state(
    0xCAFEBABEUL, 0xDEADBEEFUL, 0xFEEDFACEUL, 0x0123456789ABCDEFUL,
  )
  for step in 0..<100 {
    let _ = step
    for k in 0..<n_pre {
      pre_fire[k] = next_f32(rng) < 0.3F
    }
    for k in 0..<n_post {
      post_fire[k] = next_f32(rng) < 0.3F
    }
    stdp_confavreux_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, Float::from_int(step) * dt, dt)
  }
  for s in 0..<n_conn {
    if (w[s]).is_nan() || (w[s]).is_inf() {
      fail("w[\{s}] = \{w[s]} not finite")
    }
    if w[s] < p.w_min - 1.0e-5F {
      fail("w[\{s}] = \{w[s]} below w_min = \{p.w_min}")
    }
    if w[s] > p.w_max + 1.0e-5F {
      fail("w[\{s}] = \{w[s]} above w_max = \{p.w_max}")
    }
  }
}
