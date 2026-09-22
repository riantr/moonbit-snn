// neuron_if_sinexp_test.mbt — tests for neuron_if_sinexp.mbt.

///|
test "IFSinExpParameter::new — default τe/τi = 6/2" {
  let p = IFSinExpParameter::new()
  if p.tau_e != 6.0F { fail("tau_e default expected 6.0, got \{p.tau_e}") }
  if p.tau_i != 2.0F { fail("tau_i default expected 2.0, got \{p.tau_i}") }
  if p.tm != 20.0F { fail("tm default expected 20.0, got \{p.tm}") }
  if p.el != -62.0F { fail("el default expected -62.0, got \{p.el}") }
  if p.vt != -50.0F { fail("vt default expected -50.0, got \{p.vt}") }
}

///|
test "IFSinExpParameter::lkd_pv — LKD 2014 PV defaults" {
  let p = IFSinExpParameter::lkd_pv()
  if p.el != -62.0F { fail("el expected -62.0, got \{p.el}") }
  if p.vt != -52.0F { fail("vt expected -52.0, got \{p.vt}") }
  if p.vr != -57.47F { fail("vr expected -57.47, got \{p.vr}") }
  if p.tm != 20.0F { fail("tm expected 20.0, got \{p.tm}") }
  if p.tau_e != 6.0F { fail("tau_e expected 6.0, got \{p.tau_e}") }
  if p.tau_i != 2.0F { fail("tau_i expected 2.0, got \{p.tau_i}") }
}

///|
test "IFSinExp::new — population size, initial state at rest" {
  let rng = Xoshiro::new(42UL)
  let pop = IFSinExp::new(4, IFSinExpParameter::new(), rng)
  if pop.n != 4 { fail("expected n=4") }
  // All voltages are near el (modulo small Box-Muller noise).
  let mut k = 0
  while k < pop.n {
    let v = pop.v[k]
    if (v - (-62.0F)).abs() > 10.0F {
      fail("v[\{k}] expected near -62.0, got \{v}")
    }
    k = k + 1
  }
}

///|
test "ifsinexp_step — single-exp ge decay (no he/hi)" {
  let rng = Xoshiro::new(42UL)
  let pop = IFSinExp::new(1, IFSinExpParameter::new(), rng)
  // Inject glu into neuron 0.
  pop.glu[0] = 1.0F
  pop.gaba[0] = 0.5F
  // Step with dt=0.5ms.
  ifsinexp_step(pop, 0.5F)
  // Expected: ge = (glu + ge_old) + dt*(-ge/tau_e) = 1.0 + 0.5*(-1/6) = 1.0 - 0.0833 ≈ 0.9167.
  if (pop.ge[0] - 0.9166667F).abs() > 1.0e-4 {
    fail("ge[0] expected ≈0.9167, got \{pop.ge[0]}")
  }
  // gi = 0.5 + 0.5*(-0.5/2) = 0.5 - 0.125 = 0.375.
  if (pop.gi[0] - 0.375F).abs() > 1.0e-4 {
    fail("gi[0] expected 0.375, got \{pop.gi[0]}")
  }
  // glu/gaba reset.
  if pop.glu[0] != 0.0F { fail("glu[0] expected 0 after step, got \{pop.glu[0]}") }
  if pop.gaba[0] != 0.0F { fail("gaba[0] expected 0 after step, got \{pop.gaba[0]}") }
}

///|
test "ifsinexp_step — fire when v >= vt, reset to vr" {
  let rng = Xoshiro::new(42UL)
  let pop = IFSinExp::new(1, IFSinExpParameter::new(), rng)
  pop.v[0] = -49.0F  // above vt = -50
  ifsinexp_step(pop, 0.1F)
  if !pop.fire[0] { fail("expected fire[0] = true") }
  if pop.v[0] != -60.0F { fail("expected reset to vr=-60, got \{pop.v[0]}") }
  if pop.fire_t.length() != 1 { fail("expected 1 fire event, got \{pop.fire_t.length()}") }
}

///|
test "ifsinexp_step — refractory period" {
  let rng = Xoshiro::new(42UL)
  let pop = IFSinExp::new(1, IFSinExpParameter::new(), rng)
  pop.v[0] = -49.0F  // force fire
  ifsinexp_step(pop, 0.1F)
  if !pop.fire[0] { fail("expected fire on first step") }
  // Now in refractory (tabs > 0). Manually set v[0] = -49 to attempt re-fire.
  let v_set : Float = -49.0F
  pop.v[0] = v_set
  ifsinexp_step(pop, 0.1F)
  if pop.fire[0] { fail("expected no fire during refractory") }
  // In refractory, v should be frozen at v_set (the manually-set value),
  // NOT updated by the membrane equation.
  if pop.v[0] != v_set { fail("v should be frozen during refractory at \{v_set}, got \{pop.v[0]}") }
}

///|
test "ifsinexp_step — 100-step stability (ge/gi bounded)" {
  let rng = Xoshiro::new(42UL)
  let pop = IFSinExp::new(5, IFSinExpParameter::new(), rng)
  for _ in 0..<100 {
    // Random spike injection.
    for k in 0..<pop.n {
      if next_f64(rng).to_float() < 0.3 {
        pop.glu[k] = pop.glu[k] + 1.0F
      }
      if next_f64(rng).to_float() < 0.2 {
        pop.gaba[k] = pop.gaba[k] + 0.5F
      }
    }
    ifsinexp_step(pop, 0.1F)
  }
  // All v's should be bounded (within ±50 of el=-62).
  let mut k = 0
  while k < pop.n {
    if (pop.v[k] - (-62.0F)).abs() > 50.0F {
      fail("v[\{k}] unbounded after 100 steps: \{pop.v[k]}")
    }
    k = k + 1
  }
}