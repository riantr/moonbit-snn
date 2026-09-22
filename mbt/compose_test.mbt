// Tests for the compose / heterogeneous model dispatcher.

///|
test "compose builds HeterogeneousModel" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p1 = IF::new(2, param, rng)
  let p2 = IF::new(2, param, rng)
  let conn = SpikingSynapse::new(p1, p2, "ge")
  spiking_connect(conn, 1, 1, 5.0F)
  let model = compose([IF_(p1), IF_(p2)], [conn])
  assert_eq(model.pops.length(), 2)
  assert_eq(model.conns.length(), 1)
}

///|
test "step_heterogeneous integrates" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p1 = IF::new(1, param, rng)
  let p2 = IF::new(1, param, rng)
  let conn = SpikingSynapse::new(p1, p2, "ge")
  spiking_connect(conn, 1, 1, 50.0F)
  // Inject current into p1 so it fires
  p1.i[0] = 100.0F
  let model = compose([IF_(p1), IF_(p2)], [conn])
  for _ in 0..<200 {
    step_heterogeneous(model, 0.125F)
  }
  // After 200 steps, p1 may have fired and propagated to p2
  if p2.v[0].is_nan() {
    fail("p2.v is NaN after sim")
  }
}

///|
test "heterogeneous_sim_for runs the loop" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  // Set v close to threshold so even a small current drives a spike.
  p.v[0] = -51.0F
  p.i[0] = 1000.0F
  let model = compose([IF_(p)], [])
  heterogeneous_sim_for(model, 5.0F)
  // 5 ms = 40 steps. With i=1000, the first spike fires around step 3,
  // then the neuron resets to vr=-60, sits in 16-step refractory, then
  // rises again. If the loop ran, v is no longer -51.0F (it has either
  // been to -50 or below). If the loop never ran, v is still -51.0F.
  assert_eq(p.v[0] != -51.0F, true)
}

///|
test "compose with Poisson stimulus drives the network" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  // 1 IF neuron at v just below threshold.
  let p = IF::new(1, param, rng)
  p.v[0] = -55.0F
  // Poisson input at 5 kHz onto :ge. 5 kHz × 0.125 ms = 0.625 expected events/step.
  // Over 8000 steps we expect ~5000 events. With μ=2.0 nS each, that's enough
  // excitatory drive to push v over -50 within a few hundred ms.
  let stim = PoissonStimulusIF::new(p, "ge", 5.0F, rng)
  let model = compose([IF_(p)], [], stims=[PoissonIF_(stim)])
  heterogeneous_sim_for(model, 1000.0F)
  // After 1 s of Poisson input at 5 kHz, v should have moved (not still -55).
  assert_eq(p.v[0] != -55.0F, true)
}

///|
test "CurrentStimulus injects i_base into I[i]" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(3, param, rng)
  // 5 pA constant current to all 3 neurons.
  let stim = CurrentStimulusIF::new(p, 5.0F, rng)
  // After one stimulate! call, every I[i] should be 5.0F.
  stimulate_current_if(stim)
  assert_eq(p.i[0], 5.0F)
  assert_eq(p.i[1], 5.0F)
  assert_eq(p.i[2], 5.0F)
  // set_active(false) disables the stimulus.
  stim.set_active(false)
  p.i[0] = 99.0F
  stimulate_current_if(stim)
  // Stimulus disabled, so the user's 99.0F should NOT be overwritten.
  assert_eq(p.i[0], 99.0F)
}

///|
test "compose with CurrentStimulus drives spiking" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  // Start at v just below threshold.
  p.v[0] = -55.0F
  // 200 pA constant current — should fire within a few steps.
  let stim = CurrentStimulusIF::new(p, 200.0F, rng)
  let model = compose([IF_(p)], [], stims=[CurrentIF_(stim)])
  heterogeneous_sim_for(model, 5.0F)
  // 5 ms = 40 steps. With i=200, the neuron should fire within ~5
  // steps and reset. After 40 steps, v has been driven by the
  // current; if the loop ran, v is no longer -55.
  assert_eq(p.v[0] != -55.0F, true)
}

///|
test "get_time_heterogeneous starts at 0 and advances" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let s = SpikingSynapse::new(p, p, "ge")
  let model = compose([IF_(p)], [s])
  assert_eq(get_time_heterogeneous(model), 0.0F)
  for _ in 0..<8 {
    step_heterogeneous(model, 0.125F)
  }
  // 8 steps × 0.125ms = 1.0ms.
  let t = get_time_heterogeneous(model)
  if t < 0.99F || t > 1.01F {
    fail("time should be ~1ms after 8 steps, got " + t.to_string())
  }
}

///|
test "reset_time_heterogeneous resets clock to 0" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(1, param, rng)
  let s = SpikingSynapse::new(p, p, "ge")
  let model = compose([IF_(p)], [s])
  for _ in 0..<40 {
    step_heterogeneous(model, 0.125F)
  }
  let _ = get_time_heterogeneous(model)
  reset_time_heterogeneous(model)
  assert_eq(get_time_heterogeneous(model), 0.0F)
}
