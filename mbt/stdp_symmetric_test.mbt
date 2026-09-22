// Tests for STDPSymmetric (Festa et al. 2024 symmetric inhibitory STDP
// with zero-integral kernel) and IstdpTime (Vogels 2011 parameter type
// without dedicated step function).
//
// Mirrors SpikingNeuralNetworks.jl/test/syn/plasticity_params.jl
// "STDPSymmetric / STDPAntiSymmetric" + "iSTDPTime" testsets.

///|
test "STDPSymmetric::new: defaults match Julia" {
  let p = STDPSymmetric::new()
  if p.a_x != 0.03F {
    fail("a_x = \{p.a_x}, expected 0.03")
  }
  if p.a_y != 0.03F {
    fail("a_y = \{p.a_y}, expected 0.03")
  }
  if p.tau_x != 50.0F {
    fail("tau_x = \{p.tau_x}, expected 50.0")
  }
  if p.tau_y != 500.0F {
    fail("tau_y = \{p.tau_y}, expected 500.0")
  }
  if p.alpha_pre != 0.0F {
    fail("alpha_pre = \{p.alpha_pre}, expected 0.0")
  }
  if p.alpha_post != 0.0F {
    fail("alpha_post = \{p.alpha_post}, expected 0.0")
  }
  if p.w_max != 30.0F {
    fail("w_max = \{p.w_max}, expected 30.0")
  }
  if p.w_min != 0.0F {
    fail("w_min = \{p.w_min}, expected 0.0")
  }
}

///|
test "STDPSymmetric::new: custom values" {
  let p : STDPSymmetric = {
    a_x: 0.05F,
    a_y: 0.02F,
    tau_x: 30.0F,
    tau_y: 200.0F,
    alpha_pre: 0.1F,
    alpha_post: -0.05F,
    w_max: 100.0F,
    w_min: -10.0F,
  }
  if p.a_x != 0.05F {
    fail("a_x = \{p.a_x}, expected 0.05")
  }
  if p.alpha_pre != 0.1F {
    fail("alpha_pre = \{p.alpha_pre}, expected 0.1")
  }
  if p.w_min != -10.0F {
    fail("w_min = \{p.w_min}, expected -10.0")
  }
}

///|
test "STDPSymmetricVariables::new: zero-initialised, all four traces" {
  let v = STDPSymmetricVariables::new(5, 4)
  if v.tr_x.length() != 5 || v.tr_y.length() != 5 {
    fail("tr_x/tr_y lengths = \{v.tr_x.length()}/\{v.tr_y.length()}, expected 5/5")
  }
  if v.to_x.length() != 4 || v.to_y.length() != 4 {
    fail("to_x/to_y lengths = \{v.to_x.length()}/\{v.to_y.length()}, expected 4/4")
  }
  for k in 0..<5 {
    if v.tr_x[k] != 0.0F || v.tr_y[k] != 0.0F {
      fail("traces[\{k}] should be 0 initially")
    }
  }
  for k in 0..<4 {
    if v.to_x[k] != 0.0F || v.to_y[k] != 0.0F {
      fail("to_x/to_y[\{k}] should be 0 initially")
    }
  }
}

///|
test "STDPEntrySymmetric::new: fields populated" {
  let e = STDPEntrySymmetric::new(0, 3, 2)
  if e.conn_index != 0 || e.n_pre != 3 || e.n_post != 2 {
    fail("entry fields wrong")
  }
  if e.param.a_x != 0.03F {
    fail("default a_x = \{e.param.a_x}, expected 0.03")
  }
  if e.t_now[0] != 0.0F {
    fail("t_now should be 0")
  }
}

///|
test "STDPEntrySymmetric::change_plasticity: swaps param, preserves vars" {
  let e = STDPEntrySymmetric::new(0, 3, 3)
  e.vars.tr_x[0] = 0.7F
  e.vars.tr_y[1] = 0.5F
  e.vars.to_x[2] = 0.3F
  let new_param : STDPSymmetric = {
    a_x: 0.1F,
    a_y: 0.05F,
    tau_x: 20.0F,
    tau_y: 300.0F,
    alpha_pre: 0.01F,
    alpha_post: -0.01F,
    w_max: 50.0F,
    w_min: 0.0F,
  }
  e.change_plasticity(new_param)
  if e.param.a_x != 0.1F {
    fail("after swap, a_x = \{e.param.a_x}, expected 0.1")
  }
  if e.vars.tr_x[0] != 0.7F {
    fail("tr_x[0] should be preserved")
  }
  if e.vars.tr_y[1] != 0.5F {
    fail("tr_y[1] should be preserved")
  }
  if e.vars.to_x[2] != 0.3F {
    fail("to_x[2] should be preserved")
  }
}

///|
test "stdp_symmetric_step: pre spike adds alpha_pre + (A_x*to_x - A_y*to_y)/..." {
  // 1 pre → 1 post. Pre fires, post does not.
  // Force to_x[0]=0.5, to_y[0]=0.3.
  // Expected Δw = 0 + (0.03/(2*50)*0.5 - 0.03/(2*500)*0.3) * (post-decay factor)
  // = (0.0006 - 9e-6) = ~0.000591 (with decay ignored for simplicity).
  let w : Array[Float] = [10.0F]
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [false]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = STDPSymmetricVariables::new(1, 1)
  vars.to_x[0] = 0.5F
  vars.to_y[0] = 0.3F
  let p = STDPSymmetric::new()
  let dt = 0.125F
  stdp_symmetric_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, 0.0F, dt)
  // Compute expected: to_x and to_y both decay by (1 - dt/tau_x) and
  // (1 - dt/tau_y) respectively BEFORE the weight update. Then they
  // each bump by 1.0F since post fires. But post doesn't fire here, so
  // they just decay. Wait — actually pre fires, so to_x and to_y DON'T
  // bump (they bump on post fire). So:
  //   to_x_after = 0.5 * (1 - dt/tau_x)
  //   to_y_after = 0.3 * (1 - dt/tau_y)
  let to_x_after = 0.5F * (1.0F - dt / p.tau_x)
  let to_y_after = 0.3F * (1.0F - dt / p.tau_y)
  let dw = p.alpha_pre +
    p.a_x / (2.0F * p.tau_x) * to_x_after -
    p.a_y / (2.0F * p.tau_y) * to_y_after
  let expected = 10.0F + dw
  if (w[0] - expected).abs() > 1.0e-5F {
    fail("w[0] = \{w[0]}, expected ~\{expected}")
  }
}

///|
test "stdp_symmetric_step: post spike adds alpha_post + (A_x*tr_x - A_y*tr_y)/..." {
  // 1 pre → 1 post. Post fires, pre does not.
  let w : Array[Float] = [10.0F]
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [true]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = STDPSymmetricVariables::new(1, 1)
  vars.tr_x[0] = 0.5F
  vars.tr_y[0] = 0.3F
  let p = STDPSymmetric::new()
  let dt = 0.125F
  stdp_symmetric_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, 0.0F, dt)
  // tr_x and tr_y both decay (pre doesn't fire, so no bump).
  let tr_x_after = 0.5F * (1.0F - dt / p.tau_x)
  let tr_y_after = 0.3F * (1.0F - dt / p.tau_y)
  let dw = p.alpha_post +
    p.a_x / (2.0F * p.tau_x) * tr_x_after -
    p.a_y / (2.0F * p.tau_y) * tr_y_after
  let expected = 10.0F + dw
  if (w[0] - expected).abs() > 1.0e-5F {
    fail("w[0] = \{w[0]}, expected ~\{expected}")
  }
}

///|
test "stdp_symmetric_step: weight clamps at w_max (LTP side)" {
  // Repeated pre + post firing drives weight toward w_max.
  let w : Array[Float] = [29.5F] // start near w_max
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [true]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = STDPSymmetricVariables::new(1, 1)
  let p = STDPSymmetric::new()
  let dt = 0.125F
  for _ in 0..<100000 {
    stdp_symmetric_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, 0.0F, dt)
    // Reset traces each step so the bump is constant.
    vars.tr_x[0] = 1.0F
    vars.tr_y[0] = 1.0F
    vars.to_x[0] = 1.0F
    vars.to_y[0] = 1.0F
  }
  if (w[0] - 30.0F).abs() > 1.0e-3F {
    fail("w[0] = \{w[0]}, expected clamp at w_max=30.0")
  }
}

///|
test "stdp_symmetric_step: weights stay finite over many steps" {
  let n_pre = 4
  let n_post = 4
  let n_conn = 16
  let w : Array[Float] = Array::make(n_conn, 1.0F)
  let pre_fire : Array[Bool] = Array::make(n_pre, true)
  let post_fire : Array[Bool] = Array::make(n_post, true)
  let rowptr : Array[Int] = [0, 4, 8, 12, 16]
  let colptr : Array[Int] = [
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
  ]
  let vars = STDPSymmetricVariables::new(n_pre, n_post)
  let p = STDPSymmetric::new()
  let dt = 0.125F
  for step in 0..<200 {
    let _ = step
    stdp_symmetric_step(w, pre_fire, post_fire, colptr, rowptr, vars, p, Float::from_int(step) * dt, dt)
  }
  for s in 0..<n_conn {
    if (w[s]).is_nan() || (w[s]).is_inf() {
      fail("w[\{s}] = \{w[s]} not finite")
    }
  }
}

///|
test "IstdpTime::new: defaults match Julia" {
  let p = IstdpTime::new()
  if p.eta != 0.01F {
    fail("eta = \{p.eta}, expected 0.01")
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
test "IstdpTime: custom fields preserved" {
  let p : IstdpTime = { eta: 0.02F, tau_y: 80.0F, w_max: 500.0F, w_min: 0.05F }
  if p.eta != 0.02F {
    fail("eta = \{p.eta}, expected 0.02")
  }
  if p.tau_y != 80.0F {
    fail("tau_y = \{p.tau_y}, expected 80.0")
  }
}