// Tests for IstdpPotential (Vogels 2011 inhibitory STDP with
// potential-based post-synaptic trace).
//
// Mirrors SpikingNeuralNetworks.jl/test/syn/with_plasticity.jl
// "iSTDPPotential" testset. The Julia test only asserts that weights
// remain finite after `train!`; we replicate that plus directional /
// clamping tests to exercise the rule body.

///|
test "IstdpPotential::new: defaults match Julia" {
  let p = IstdpPotential::new()
  if p.eta != 0.001F {
    fail("eta = \{p.eta}, expected 0.001")
  }
  if p.v0 != -50.0F {
    fail("v0 = \{p.v0}, expected -50.0 (mV)")
  }
  if p.tau_y != 200.0F {
    fail("tau_y = \{p.tau_y}, expected 200.0")
  }
  if p.w_max != 243.0F {
    fail("w_max = \{p.w_max}, expected 243.0")
  }
  if p.w_min != 0.01F {
    fail("w_min = \{p.w_min}, expected 0.01")
  }
}

///|
test "IstdpPotentialVariables::new: zero-initialised" {
  let v = IstdpPotentialVariables::new(5, 4)
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
test "IstdpPotentialEntry::new: fields populated" {
  let e = IstdpPotentialEntry::new(0, 3, 2)
  if e.conn_index != 0 {
    fail("conn_index = \{e.conn_index}, expected 0")
  }
  if e.n_pre != 3 || e.n_post != 2 {
    fail("n_pre/n_post = \{e.n_pre}/\{e.n_post}, expected 3/2")
  }
  if e.t_now[0] != 0.0F {
    fail("t_now should be 0 initially")
  }
  if e.param.eta != 0.001F {
    fail("default eta = \{e.param.eta}, expected 0.001")
  }
}

///|
test "IstdpPotentialEntry::change_plasticity: swaps param, preserves vars" {
  let e = IstdpPotentialEntry::new(0, 3, 3)
  e.vars.tpre[0] = 0.7F
  e.vars.tpost[1] = 0.5F
  let new_param : IstdpPotential = {
    eta: 0.05F,
    v0: -40.0F,
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
test "istdp_potential_step: tpre decays to 0; tpost low-pass filters v_post" {
  // tpre[j] decay toward 0 (same as IstdpRate).
  // tpost[i] low-pass filter toward v_post[i].
  //   tpost += dt * -(tpost - v_post) / tau_y
  // After one step with v_post[0]=60.0F (well above tpost=0):
  //   tpost_after = 0 + dt * -(0 - 60) * inv_tau_y = dt * 60 / tau_y
  //                = 0.125 * 60 / 200 = 0.0375.
  let w : Array[Float] = [10.0F]
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [false]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let v_post : Array[Float] = [60.0F]
  let vars = IstdpPotentialVariables::new(1, 1)
  vars.tpre[0] = 1.0F
  vars.tpost[0] = 0.0F
  let p = IstdpPotential::new()
  let dt = 0.125F
  istdp_potential_step(w, pre_fire, post_fire, colptr, rowptr, v_post, vars, p, 0.0F, dt)
  // tpre: 1.0 * (1 - dt/tau_y) = 1 * 0.999375 = 0.999375.
  let tpre_expected : Float = 1.0F * (1.0F - dt / p.tau_y)
  if (vars.tpre[0] - tpre_expected).abs() > 1.0e-5F {
    fail("tpre[0] = \{vars.tpre[0]}, expected ~\{tpre_expected}")
  }
  // tpost: 0 + dt * (60 - 0) / tau_y = 0.125 * 60 / 200 = 0.0375.
  let tpost_expected : Float = dt * 60.0F / p.tau_y
  if (vars.tpost[0] - tpost_expected).abs() > 1.0e-5F {
    fail("tpost[0] = \{vars.tpost[0]}, expected ~\{tpost_expected}")
  }
}

///|
test "istdp_potential_step: pre spike adds eta * (tpost - v0)" {
  // 1 pre → 1 post. Pre fires, post does not.
  // v_post[0] = 60.0F → tpost[0] after step = 60 * dt/tau_y = 0.0375.
  // Δw = 0.001 * (0.0375 - (-50)) = 0.001 * 50.0375 = 0.0500375.
  let w : Array[Float] = [10.0F]
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [false]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let v_post : Array[Float] = [60.0F]
  let vars = IstdpPotentialVariables::new(1, 1)
  vars.tpost[0] = 0.0F
  let p = IstdpPotential::new()
  let dt = 0.125F
  istdp_potential_step(w, pre_fire, post_fire, colptr, rowptr, v_post, vars, p, 0.0F, dt)
  let tpost_after = dt * 60.0F / p.tau_y
  let dw = p.eta * (tpost_after - p.v0)
  let expected = 10.0F + dw
  if (w[0] - expected).abs() > 1.0e-5F {
    fail("w[0] = \{w[0]}, expected ~\{expected}")
  }
}

///|
test "istdp_potential_step: post spike adds eta * tpre[j]" {
  // 1 pre → 1 post. Post fires, pre does not. tpre[0] forced to 0.7.
  let w : Array[Float] = [10.0F]
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [true]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let v_post : Array[Float] = [60.0F]
  let vars = IstdpPotentialVariables::new(1, 1)
  vars.tpre[0] = 0.7F
  let p = IstdpPotential::new()
  let dt = 0.125F
  istdp_potential_step(w, pre_fire, post_fire, colptr, rowptr, v_post, vars, p, 0.0F, dt)
  let tpre_after : Float = 0.7F * (1.0F - dt / p.tau_y)
  let dw = p.eta * tpre_after
  let expected = 10.0F + dw
  if (w[0] - expected).abs() > 1.0e-5F {
    fail("w[0] = \{w[0]}, expected ~\{expected}")
  }
}

///|
test "istdp_potential_step: weight clamps at w_max (LTP side)" {
  // Repeated post firing drives weight toward w_max.
  let w : Array[Float] = [240.0F]
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [true]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let v_post : Array[Float] = [60.0F]
  let vars = IstdpPotentialVariables::new(1, 1)
  vars.tpre[0] = 1.0F
  let p = IstdpPotential::new()
  let dt = 0.125F
  // Need many iterations because eta is much smaller (0.001 vs 0.01).
  for _ in 0..<200000 {
    istdp_potential_step(w, pre_fire, post_fire, colptr, rowptr, v_post, vars, p, 0.0F, dt)
    vars.tpre[0] = 1.0F
  }
  if (w[0] - 243.0F).abs() > 1.0e-3F {
    fail("w[0] = \{w[0]}, expected clamp at w_max=243.0")
  }
}

///|
test "istdp_potential_step: weights stay finite over many steps" {
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
  let v_post : Array[Float] = Array::make(n_post, 60.0F)
  let vars = IstdpPotentialVariables::new(n_pre, n_post)
  let p = IstdpPotential::new()
  let dt = 0.125F
  for step in 0..<200 {
    let _ = step
    istdp_potential_step(w, pre_fire, post_fire, colptr, rowptr, v_post, vars, p, Float::from_int(step) * dt, dt)
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

///|
test "istdp_potential_step: integration — random drive + varying v_post" {
  // Mixed drive, varying v_post[i] each step. After 100 steps, weights
  // remain finite and clamped.
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
  let v_post : Array[Float] = Array::make(n_post, 0.0F)
  let vars = IstdpPotentialVariables::new(n_pre, n_post)
  let p = IstdpPotential::new()
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
      // Vary v_post in [-70, 50] mV.
      v_post[k] = -70.0F + 120.0F * next_f32(rng)
    }
    istdp_potential_step(w, pre_fire, post_fire, colptr, rowptr, v_post, vars, p, Float::from_int(step) * dt, dt)
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