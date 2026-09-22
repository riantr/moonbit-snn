// Tests for IstdpRate (Vogels 2011 inhibitory STDP with rate homeostasis).
//
// Mirrors SpikingNeuralNetworks.jl/test/syn/with_plasticity.jl
// "iSTDPRate" testset. The Julia test only asserts that weights remain
// finite after `train!`; we replicate that plus directional / clamping
// tests to exercise the rule body.

///|
test "IstdpRate::new: defaults match Julia" {
  let p = IstdpRate::new()
  if p.eta != 0.01F {
    fail("eta = \{p.eta}, expected 0.01")
  }
  // r = 3 Hz = 3 * hz = 3 * 0.001 = 0.003 (internal units rate/ms).
  if (p.r - 3.0F * hz).abs() > 1.0e-6F {
    fail("r = \{p.r}, expected 3.0 * hz")
  }
  if p.tau_y != 50.0F {
    fail("tau_y = \{p.tau_y}, expected 50.0")
  }
  if p.w_max != 243.0F {
    fail("w_max = \{p.w_max}, expected 243.0")
  }
  if p.w_min != 0.01F {
    fail("w_min = \{p.w_min}, expected 0.01")
  }
}

///|
test "IstdpRateVariables::new: zero-initialised" {
  let v = IstdpRateVariables::new(5, 4)
  if v.tpre.length() != 5 {
    fail("tpre length = \{v.tpre.length()}, expected 5")
  }
  if v.tpost.length() != 4 {
    fail("tpost length = \{v.tpost.length()}, expected 4")
  }
  for k in 0..<5 {
    if v.tpre[k] != 0.0F {
      fail("tpre[\{k}] = \{v.tpre[k]} should be 0 initially")
    }
  }
  for k in 0..<4 {
    if v.tpost[k] != 0.0F {
      fail("tpost[\{k}] = \{v.tpost[k]} should be 0 initially")
    }
  }
}

///|
test "IstdpRateEntry::new: fields populated" {
  let e = IstdpRateEntry::new(0, 3, 2)
  if e.conn_index != 0 {
    fail("conn_index = \{e.conn_index}, expected 0")
  }
  if e.n_pre != 3 || e.n_post != 2 {
    fail("n_pre/n_post = \{e.n_pre}/\{e.n_post}, expected 3/2")
  }
  if e.t_now[0] != 0.0F {
    fail("t_now should be 0 initially")
  }
  // Defaults match Julia.
  if e.param.eta != 0.01F {
    fail("default eta = \{e.param.eta}, expected 0.01")
  }
}

///|
test "IstdpRateEntry::change_plasticity: swaps param, preserves vars" {
  let e = IstdpRateEntry::new(0, 3, 3)
  e.vars.tpre[0] = 0.7F
  e.vars.tpost[1] = 0.5F
  let new_param : IstdpRate = {
    eta: 0.05F,
    r: 0.1F * hz,
    tau_y: 100.0F,
    w_max: 50.0F,
    w_min: -1.0F,
  }
  e.change_plasticity(new_param)
  if e.param.eta != 0.05F {
    fail("after swap, eta = \{e.param.eta}, expected 0.05")
  }
  if e.vars.tpre[0] != 0.7F {
    fail("vars.tpre[0] should be preserved")
  }
  if e.vars.tpost[1] != 0.5F {
    fail("vars.tpost[1] should be preserved")
  }
}

///|
test "istdp_rate_step: trace decay + spike bump" {
  // After one step with no firing, tpre/tpost should be multiplied by
  // (1 - dt/tau_y). With dt=0.125, tau_y=50: factor = 1 - 0.0025 = 0.9975.
  let w : Array[Float] = [10.0F]
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [false]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = IstdpRateVariables::new(1, 1)
  vars.tpre[0] = 1.0F
  vars.tpost[0] = 1.0F
  let p = IstdpRate::new()
  let dt = 0.125F
  istdp_rate_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, 0.0F, dt)
  let decay : Float = 1.0F - dt / p.tau_y
  // tpre after decay (no fire) = 1 * decay = 0.9975
  if (vars.tpre[0] - decay).abs() > 1.0e-5F {
    fail("tpre[0] = \{vars.tpre[0]}, expected ~\{decay}")
  }
  if (vars.tpost[0] - decay).abs() > 1.0e-5F {
    fail("tpost[0] = \{vars.tpost[0]}, expected ~\{decay}")
  }
}

///|
test "istdp_rate_step: pre spike adds eta * (tpost - 2 * r * tau_y)" {
  // 1 pre → 1 post. Pre fires, post does not. tpost[0] forced to 0.5
  // before the step. Expected Δw = 0.01 * (0.5*decay - 2 * 0.003 * 50)
  //                                = 0.01 * (0.49875 - 0.3)
  //                                = 0.01 * 0.19875 = 0.0019875.
  let w : Array[Float] = [10.0F]
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [false]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = IstdpRateVariables::new(1, 1)
  vars.tpost[0] = 0.5F
  let p = IstdpRate::new()
  let dt = 0.125F
  istdp_rate_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, 0.0F, dt)
  let decay : Float = 1.0F - dt / p.tau_y
  let tpost_after = 0.5F * decay
  let dw = p.eta * (tpost_after - 2.0F * p.r * p.tau_y)
  let expected = 10.0F + dw
  if (w[0] - expected).abs() > 1.0e-5F {
    fail("w[0] = \{w[0]}, expected ~\{expected}")
  }
}

///|
test "istdp_rate_step: post spike adds eta * tpre[j]" {
  // 1 pre → 1 post. Post fires, pre does not. tpre[0] forced to 0.7.
  // Expected Δw = 0.01 * (0.7 * decay) ≈ 0.01 * 0.698 = 0.00698.
  let w : Array[Float] = [10.0F]
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [true]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = IstdpRateVariables::new(1, 1)
  vars.tpre[0] = 0.7F
  let p = IstdpRate::new()
  let dt = 0.125F
  istdp_rate_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, 0.0F, dt)
  let decay : Float = 1.0F - dt / p.tau_y
  let dw = p.eta * (0.7F * decay)
  let expected = 10.0F + dw
  if (w[0] - expected).abs() > 1.0e-5F {
    fail("w[0] = \{w[0]}, expected ~\{expected}")
  }
}

///|
test "istdp_rate_step: weight clamps at w_max (LTP side)" {
  // Repeated post firing drives weight toward w_max.
  let w : Array[Float] = [240.0F] // start near w_max
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [true]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = IstdpRateVariables::new(1, 1)
  vars.tpre[0] = 1.0F
  let p = IstdpRate::new()
  let dt = 0.125F
  // Run many iterations with post firing each step; reset tpre
  // each step so the bump is constant.
  for _ in 0..<50000 {
    istdp_rate_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, 0.0F, dt)
    vars.tpre[0] = 1.0F
  }
  if (w[0] - 243.0F).abs() > 1.0e-3F {
    fail("w[0] = \{w[0]}, expected clamp at w_max=243.0")
  }
}

///|
test "istdp_rate_step: weights stay finite over many steps" {
  // Sanity check matching Julia's `all(isfinite, syn.W)` assertion.
  let n_pre = 4
  let n_post = 4
  let n_conn = 16
  let w : Array[Float] = Array::make(n_conn, 100.0F)
  let pre_fire : Array[Bool] = Array::make(n_pre, true)
  let post_fire : Array[Bool] = Array::make(n_post, true)
  let rowptr : Array[Int] = [0, 4, 8, 12, 16]
  let colptr : Array[Int] = [
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
  ]
  let vars = IstdpRateVariables::new(n_pre, n_post)
  let p = IstdpRate::new()
  let dt = 0.125F
  for step in 0..<200 {
    let _ = step
    istdp_rate_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, Float::from_int(step) * dt, dt)
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

///|
test "istdp_rate_step: integration — random pre/post spikes" {
  // Mixed pre/post firing across a 4×4 dense synapse for 100 steps.
  let n_pre = 4
  let n_post = 4
  let n_conn = 16
  let w : Array[Float] = Array::make(n_conn, 100.0F)
  let pre_fire : Array[Bool] = Array::make(n_pre, false)
  let post_fire : Array[Bool] = Array::make(n_post, false)
  let rowptr : Array[Int] = [0, 4, 8, 12, 16]
  let colptr : Array[Int] = [
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
  ]
  let vars = IstdpRateVariables::new(n_pre, n_post)
  let p = IstdpRate::new()
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
    istdp_rate_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, Float::from_int(step) * dt, dt)
  }
  for s in 0..<n_conn {
    if (w[s]).is_nan() || (w[s]).is_inf() {
      fail("w[\{s}] = \{w[s]} not finite")
    }
    if w[s] < p.w_min - 1.0e-5F {
      fail("w[\{s}] = \{w[s]} below w_min")
    }
    if w[s] > p.w_max + 1.0e-5F {
      fail("w[\{s}] = \{w[s]} above w_max")
    }
  }
}