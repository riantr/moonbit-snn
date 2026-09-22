// Tests for BalancedParameter + BalancedStimulus (feedback-driven Poisson
// generator). Mirrors SpikingNeuralNetworks.jl/test/stim/balanced.jl.

///|
test "BalancedParameter: defaults match Julia" {
  let p = BalancedParameter::new()
  if p.kIE != 1.0F {
    fail("kIE = \{p.kIE}, expected 1.0")
  }
  if p.beta != 0.0F {
    fail("beta = \{p.beta}, expected 0.0")
  }
  if p.tau != 50.0F {
    fail("tau = \{p.tau}, expected 50.0")
  }
  // r0 default is 1 kHz = 1.0F (khz = 1.0F).
  if p.r0 != 1.0F {
    fail("r0 = \{p.r0}, expected 1.0 (1 kHz)")
  }
  if p.w != 1.0F {
    fail("w = \{p.w}, expected 1.0")
  }
  if p.wIE != 1.0F {
    fail("wIE = \{p.wIE}, expected 1.0")
  }
  if p.same_input != false {
    fail("same_input should default to false")
  }
}

///|
test "BalancedParameter: custom values" {
  let p = BalancedParameter::new(
    kIE=2.0F,
    beta=0.1F,
    tau=100.0F,
    r0=2.0F * khz,
    w=2.0F,
    wIE=3.0F,
    same_input=true,
  )
  if p.kIE != 2.0F {
    fail("kIE = \{p.kIE}, expected 2.0")
  }
  if p.beta != 0.1F {
    fail("beta = \{p.beta}, expected 0.1")
  }
  if p.tau != 100.0F {
    fail("tau = \{p.tau}, expected 100.0")
  }
  if p.r0 != 2.0F {
    fail("r0 = \{p.r0}, expected 2.0 (2 kHz)")
  }
  if p.w != 2.0F {
    fail("w = \{p.w}, expected 2.0")
  }
  if p.wIE != 3.0F {
    fail("wIE = \{p.wIE}, expected 3.0")
  }
  if p.same_input != true {
    fail("same_input should be true")
  }
}

///|
test "BalancedStimulus::new: zero-initialized state" {
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let pop = IF::new(8, if_param, rng)
  let s = BalancedStimulus::new(pop, sym_e="ge", sym_i="gi", seed=0xDEADBEEFUL)
  if s.n != 8 {
    fail("N = \{s.n}, expected 8")
  }
  // r[i] should equal r0 (default 1.0 internal = 1 kHz).
  for i in 0..<s.n {
    if (s.r[i] - 1.0F).abs() > 1.0e-6F {
      fail("r[\{i}] = \{s.r[i]}, expected 1.0")
    }
    if s.noise[i] != 0.0F {
      fail("noise[\{i}] = \{s.noise[i]}, expected 0.0")
    }
    if s.fire[i] != false {
      fail("fire[\{i}] should be false initially")
    }
  }
}

///|
test "BalancedStimulus::new: drives post.receptors (alias-check via stimulate)" {
  // The constructor sets ge/gi to pop.glu/pop.gaba. Verify by running
  // a few steps and confirming pop.glu/pop.gaba accumulate 鈥?if the
  // arrays were independent, post-synaptic receptors would stay at 0.
  let rng = Xoshiro::default()
  let pop = IF::new(4, IFParameter::new(), rng)
  let p = BalancedParameter::new(r0=2.0F * khz, w=1.0F, wIE=1.0F)
  let s = BalancedStimulus::new(pop, sym_e="ge", sym_i="gi", param=p, seed=1UL)
  let dt = 0.125F
  for _ in 0..<20 {
    stimulate_balanced(s, 0.0F, dt)
  }
  let mut ge_sum = 0.0F
  let mut gi_sum = 0.0F
  for i in 0..<pop.n {
    ge_sum = ge_sum + pop.glu[i]
    gi_sum = gi_sum + pop.gaba[i]
  }
  if ge_sum <= 0.0F {
    fail("pop.glu should accumulate excitatory drive (got \{ge_sum})")
  }
  if gi_sum <= 0.0F {
    fail("pop.gaba should accumulate inhibitory drive (got \{gi_sum})")
  }
}

///|
test "stimulate_balanced: writes to ge/gi, fires sometimes" {
  // High rate 鈫?strong drive 鈫?most neurons should fire at least once.
  let rng = Xoshiro::default()
  let pop = IF::new(20, IFParameter::new(), rng)
  let p = BalancedParameter::new(
    kIE=2.0F,
    beta=0.0F,
    r0=2.0F * khz, // 2 kHz 鈫?strong drive
    w=1.0F,
    wIE=1.0F,
  )
  let s = BalancedStimulus::new(pop, sym_e="ge", sym_i="gi", param=p, seed=42UL)
  let dt = 0.125F
  let n_steps = 100
  let mut ge_total = 0.0F
  let mut gi_total = 0.0F
  let mut fires = 0
  for _ in 0..<n_steps {
    stimulate_balanced(s, 0.0F, dt)
    for i in 0..<s.n {
      ge_total = ge_total + s.ge[i]
      gi_total = gi_total + s.gi[i]
      if s.fire[i] {
        fires = fires + 1
      }
    }
  }
  // With 2 kHz baseline 脳 0.125 ms 脳 100 steps 脳 20 neurons, the
  // expected total ge contribution is ~ 20 * 2.0 * 100 * 0.125 * 1.0 = 500.
  if ge_total < 50.0F {
    fail("ge_total = \{ge_total}, expected significant excitatory drive")
  }
  if gi_total < 50.0F {
    fail("gi_total = \{gi_total}, expected significant inhibitory drive")
  }
  // Most neurons should fire at least once over 100 steps.
  if fires < 50 {
    fail("fires = \{fires}, expected many neuron-step firings")
  }
}

///|
test "stimulate_balanced: zero rate 鈫?no change" {
  let rng = Xoshiro::default()
  let pop = IF::new(5, IFParameter::new(), rng)
  let p = BalancedParameter::new(r0=0.0F, w=1.0F, wIE=1.0F)
  let s = BalancedStimulus::new(pop, sym_e="ge", sym_i="gi", param=p, seed=2UL)
  stimulate_balanced(s, 0.0F, 0.125F)
  for i in 0..<s.n {
    if s.ge[i] != 0.0F {
      fail("ge[\{i}] = \{s.ge[i]} should be 0 with r0=0")
    }
    if s.gi[i] != 0.0F {
      fail("gi[\{i}] = \{s.gi[i]} should be 0 with r0=0")
    }
    if s.fire[i] != false {
      fail("fire[\{i}] should be false with r0=0")
    }
  }
}

///|
test "stimulate_balanced: rate adaptation drives r toward r0" {
  // Initial r=0 (set by overriding default), constant drive: r should
  // climb toward r0 with time constant 400ms. Run for 3 seconds (well
  // past the adaptation time constant) and confirm r is within 5% of r0.
  let rng = Xoshiro::default()
  let pop = IF::new(1, IFParameter::new(), rng)
  let p = BalancedParameter::new(
    beta=0.0F,
    r0=1.0F * khz, // 1 kHz internal = 1.0
    w=1.0F,
    wIE=1.0F,
  )
  let s = BalancedStimulus::new(pop, sym_e="ge", sym_i="gi", param=p, seed=7UL)
  // Force r[0]=0 to start adaptation from zero.
  s.r[0] = 0.0F
  let dt = 0.125F
  let n_steps = 24000 // 3 seconds → 1 - exp(-3000/400) ≈ 0.9994
  for _ in 0..<n_steps {
    stimulate_balanced(s, 0.0F, dt)
  }
  // After 3 s of adaptation toward r0=1.0, r should be very close.
  let r_final = s.r[0]
  if (r_final - 1.0F).abs() > 0.05F {
    fail("r_final = \{r_final}, expected ~1.0 (r0)")
  }
}

///|
test "stimulate_balanced: same_input=true broadcasts ge to all neurons" {
  let rng = Xoshiro::default()
  let pop = IF::new(6, IFParameter::new(), rng)
  let p = BalancedParameter::new(
    beta=0.0F,
    r0=1.0F * khz,
    w=2.0F,
    wIE=1.0F,
    same_input=true,
  )
  let s = BalancedStimulus::new(pop, sym_e="ge", sym_i="gi", param=p, seed=99UL)
  let dt = 0.125F
  // Run for 100 steps and capture ge values at the end.
  for _ in 0..<100 {
    stimulate_balanced(s, 0.0F, dt)
  }
  // When same_input=true, every step that fires adds the same value to
  // every ge[i]. So all ge[i] should be equal after a sequence of steps.
  let g0 = s.ge[0]
  for i in 1..<s.n {
    if (s.ge[i] - g0).abs() > 1.0e-6F {
      fail("ge[\{i}] = \{s.ge[i]} should equal ge[0] = \{g0} in same_input mode")
    }
  }
}

///|
test "reset_balanced: clears r, noise, fire" {
  let rng = Xoshiro::default()
  let pop = IF::new(3, IFParameter::new(), rng)
  let p = BalancedParameter::new(r0=3.0F * khz, w=1.0F)
  let s = BalancedStimulus::new(pop, sym_e="ge", sym_i="gi", param=p, seed=11UL)
  let dt = 0.125F
  for _ in 0..<50 {
    stimulate_balanced(s, 0.0F, dt)
  }
  // After 50 steps, fire/r/noise should be non-trivial. Now reset.
  reset_balanced(s)
  for i in 0..<s.n {
    if (s.r[i] - 3.0F).abs() > 1.0e-6F {
      fail("after reset, r[\{i}] = \{s.r[i]}, expected r0=3.0")
    }
    if s.noise[i] != 0.0F {
      fail("after reset, noise[\{i}] = \{s.noise[i]}, expected 0")
    }
    if s.fire[i] != false {
      fail("after reset, fire[\{i}] should be false")
    }
  }
}
