// Tests for vSTDPParameter (Litwin-Kumar-Doiron 2014).

///|
test "VstdpParameter::new: defaults match Julia" {
  let p = VstdpParameter::new()
  if (p.a_ltd - 1.0e-3F).abs() > 1.0e-9F {
    fail("a_ltd = \{p.a_ltd}, expected 1e-3")
  }
  if (p.a_ltp - 2.0e-3F).abs() > 1.0e-9F {
    fail("a_ltp = \{p.a_ltp}, expected 2e-3")
  }
  if (p.theta_ltd - 1.0F).abs() > 1.0e-6F {
    fail("theta_ltd = \{p.theta_ltd}, expected 1.0")
  }
  if (p.theta_ltp - 1.0F).abs() > 1.0e-6F {
    fail("theta_ltp = \{p.theta_ltp}, expected 1.0")
  }
}

///|
test "VstdpVariables::new: shape and zeros" {
  let v = VstdpVariables::new(3, 4)
  if v.w.length() != 12 {
    fail("w.length = \{v.w.length()}, expected 12")
  }
  for i in 0..<12 {
    if v.w[i] != 0.0F {
      fail("w[\{i}] = \{v.w[i]}, expected 0")
    }
  }
}

///|
test "vstdp_step: pre-fire + high v_post → LTD" {
  let v = VstdpVariables::new(1, 1)
  v.w[0] = 0.01F
  let p = VstdpParameter::new()
  let pre_v : Array[Float] = [-50.0F]
  let post_v : Array[Float] = [10.0F]   // > θ_ltd
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [false]
  vstdp_step(v, p, pre_v, post_v, pre_fire, post_fire)
  // Expected: w = 0.01 - 1e-3 = 0.009
  if (v.w[0] - 9.0e-3F).abs() > 1.0e-6F {
    fail("w[0] = \{v.w[0]}, expected 0.009")
  }
}

///|
test "vstdp_step: post-fire + high v_pre → LTP" {
  let v = VstdpVariables::new(1, 1)
  v.w[0] = 0.01F
  let p = VstdpParameter::new()
  let pre_v : Array[Float] = [10.0F]   // > θ_ltp
  let post_v : Array[Float] = [-50.0F]
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [true]
  vstdp_step(v, p, pre_v, post_v, pre_fire, post_fire)
  // Expected: w = 0.01 + 2e-3 = 0.012
  if (v.w[0] - 0.012F).abs() > 1.0e-6F {
    fail("w[0] = \{v.w[0]}, expected 0.012")
  }
}

///|
test "vstdp_step: low voltages → no weight change" {
  let v = VstdpVariables::new(2, 2)
  for i in 0..<4 {
    v.w[i] = 0.5F
  }
  let p = VstdpParameter::new()
  let pre_v : Array[Float] = [-60.0F, -70.0F]
  let post_v : Array[Float] = [-60.0F, -70.0F]
  let pre_fire : Array[Bool] = [true, false]
  let post_fire : Array[Bool] = [false, true]
  vstdp_step(v, p, pre_v, post_v, pre_fire, post_fire)
  for i in 0..<4 {
    if (v.w[i] - 0.5F).abs() > 1.0e-6F {
      fail("w[\{i}] = \{v.w[i]}, expected 0.5 (no change)")
    }
  }
}

///|
test "vstdp_step: weight clamps at w_max (LTP)" {
  let v = VstdpVariables::new(1, 1)
  v.w[0] = 0.0F
  let p = VstdpParameter::new()
  // Multiple LTP steps should saturate at w_max = 50.0.
  let pre_v : Array[Float] = [10.0F]
  let post_v : Array[Float] = [-50.0F]
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [true]
  for _ in 0..<100000 {
    vstdp_step(v, p, pre_v, post_v, pre_fire, post_fire)
  }
  // After many LTP steps with a_ltp=2e-3, weight should reach
  // ~50 (clamped) but Float32 may land at 49.999... or 50.0.
  if v.w[0] < 49.0F || v.w[0] > 50.0F {
    fail("w[0] = \{v.w[0]}, expected ~50 (clamped, range 49-50)")
  }
}

///|
test "vstdp_step: weight clamps at w_min (LTD)" {
  let v = VstdpVariables::new(1, 1)
  v.w[0] = 0.05F
  let p = VstdpParameter::new()
  let pre_v : Array[Float] = [-50.0F]
  let post_v : Array[Float] = [10.0F]
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [false]
  for _ in 0..<100 {
    vstdp_step(v, p, pre_v, post_v, pre_fire, post_fire)
  }
  // After 100 LTD steps with a_ltd=1e-3, weight should reach 0 (clamped).
  if v.w[0] > 0.001F {
    fail("w[0] = \{v.w[0]}, expected near 0 (clamped)")
  }
}

///|
test "vstdp_step: both pre and post fire same step (LTD then LTP)" {
  let v = VstdpVariables::new(1, 1)
  v.w[0] = 0.02F
  let p = VstdpParameter::new()
  let pre_v : Array[Float] = [10.0F]    // high
  let post_v : Array[Float] = [10.0F]  // high
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [true]
  vstdp_step(v, p, pre_v, post_v, pre_fire, post_fire)
  // First pre→LTD: w = 0.02 - 1e-3 = 0.019
  // Then post→LTP: w = 0.019 + 2e-3 = 0.021
  if (v.w[0] - 0.021F).abs() > 1.0e-6F {
    fail("w[0] = \{v.w[0]}, expected 0.021 (LTD then LTP)")
  }
}

///|
test "vstdp_step: multi-pre multi-post (Julia vSTDP test pattern)" {
  // Mirrors Julia's _driven_model(Npre=20, Npost=5, ltp=vSTDPParameter()).
  // x is sized Npre, not Npost — that's a Julia regression test
  // (vSTDPVariables.x[i] indexed by pre-neuron j).
  let v = VstdpVariables::new(2, 3)
  // Start with non-zero weights so LTD can decrement (clamped at w_min=0).
  for i in 0..<6 {
    v.w[i] = 0.01F
  }
  let p = VstdpParameter::new()
  let pre_v : Array[Float] = [-50.0F, -50.0F]
  let post_v : Array[Float] = [5.0F, 5.0F, 5.0F]   // all > θ_ltd
  let pre_fire : Array[Bool] = [true, false]
  let post_fire : Array[Bool] = [false, false, false]
  vstdp_step(v, p, pre_v, post_v, pre_fire, post_fire)
  // j=0 connections (pre[0] fired): w[0], w[1], w[2] should be 0.01 - 1e-3 = 0.009.
  // j=1 connections (pre[1] didn't fire): w[3], w[4], w[5] should stay 0.01.
  let w_after_ltd = 0.01F - 1.0e-3F
  for i in 0..<3 {
    if (v.w[i] - w_after_ltd).abs() > 1.0e-6F {
      fail("w[0*3+\{i}] = \{v.w[i]}, expected ~\{w_after_ltd}")
    }
    if (v.w[3 + i] - 0.01F).abs() > 1.0e-6F {
      fail("w[1*3+\{i}] = \{v.w[3 + i]}, expected 0.01 (pre[1] didn't fire)")
    }
  }
}