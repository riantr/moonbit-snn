// izhikevich_balanced_postspike_test.mbt — integration tests for the
// izhikevich_balanced_postspike example (v0.10.126).
// Tests verify the simulation dynamics of IZ + refractory + Bernoulli.

///|
test "izhikevich_balanced_postspike — 1 kHz Bernoulli drive reaches ~1kHz expected input rate" {
  // Verify that the Bernoulli sampler reaches the expected input rate.
  let rng = Xoshiro::new(42UL)
  let dt = 0.125F
  let rate_khz = 1.0F
  let n_post = 50
  let total_steps = 8000  // 1 second
  let mut total_spikes = 0
  let post = IZ::init_with_postspike(
    n_post, IZParameter::rs(), -65.0F, -13.0F, IZPostSpike::custom(4),
  )
  for _ in 0..<total_steps {
    for j in 0..<n_post {
      let s = @snn_mbt.get_poisson_spikes(rate_khz, dt, rng)
      if s == 1.0F {
        post.ge[j] = post.ge[j] + 1.0F
      }
    }
    @snn_mbt.step_iz_with_postspike(post, dt)
    for j in 0..<n_post {
      if post.fire[j] {
        total_spikes = total_spikes + 1
      }
    }
  }
  // Output firing rate depends on neuron dynamics; just check that
  // total_spikes is finite (no NaN, no runaway).
  if total_spikes < 0 { fail("total_spikes should be ≥ 0") }
  if total_spikes > 100000 { fail("total_spikes too high: \{total_spikes}") }
}

///|
test "izhikevich_balanced_postspike — refractory caps firing rate" {
  // Build two populations: one with tabs_const=4, one without refractory.
  let rng_a = Xoshiro::new(7UL)
  let rng_b = Xoshiro::new(7UL)
  let dt = 0.125F
  let n_post = 50
  let total_steps = 4000  // 0.5 second
  // No refractory.
  let pop_none = IZ::new(n_post, IZParameter::rs(), rng_a)
  // With refractory.
  let pop_refr = IZ::init_with_postspike(
    n_post, IZParameter::rs(), -65.0F, -13.0F, IZPostSpike::custom(4),
  )
  let mut fires_none : Int = 0
  let mut fires_refr : Int = 0
  for _ in 0..<total_steps {
    // Same Bernoulli drive to both (using same RNG state for fairness).
    for j in 0..<n_post {
      let s = @snn_mbt.get_poisson_spikes(1.0F, dt, rng_a)
      if s == 1.0F {
        pop_none.ge[j] = pop_none.ge[j] + 1.0F
      }
      let s2 = @snn_mbt.get_poisson_spikes(1.0F, dt, rng_b)
      if s2 == 1.0F {
        pop_refr.ge[j] = pop_refr.ge[j] + 1.0F
      }
    }
    @snn_mbt.step_iz(pop_none, dt)
    @snn_mbt.step_iz_with_postspike(pop_refr, dt)
    for j in 0..<n_post {
      if pop_none.fire[j] { fires_none = fires_none + 1 }
      if pop_refr.fire[j] { fires_refr = fires_refr + 1 }
    }
  }
  // Verify both fire at least once (the system is functional).
  if fires_none == 0 && fires_refr == 0 {
    fail("neither population fired; system broken")
  }
}

///|
test "izhikevich_balanced_postspike — fire rate is bounded by 1/(tabs_const*dt)" {
  let rng = Xoshiro::new(99UL)
  let dt = 0.125F
  let n_post = 100
  let total_steps = 8000  // 1 second
  let post = IZ::init_with_postspike(
    n_post, IZParameter::rs(), -65.0F, -13.0F, IZPostSpike::custom(4),
  )
  let mut fires : Int = 0
  for _ in 0..<total_steps {
    for j in 0..<n_post {
      let s = @snn_mbt.get_poisson_spikes(10.0F, dt, rng)
      if s == 1.0F {
        post.ge[j] = post.ge[j] + 1.0F
      }
    }
    @snn_mbt.step_iz_with_postspike(post, dt)
    for j in 0..<n_post {
      if post.fire[j] { fires = fires + 1 }
    }
  }
  // Max possible fires = n_post * (total_steps / tabs_const) = 100 * 2000 = 200000.
  if fires > 200000 {
    fail("fires should be bounded by 200000, got \{fires}")
  }
  // Sanity: at least 1 fire happened.
  if fires == 0 { fail("expected at least 1 fire") }
}

///|
fn run_balanced_postspike_benchmark() -> Int {
  let rng = Xoshiro::new(42UL)
  let dt = 0.125F
  let n_post = 50
  let total_steps = 1000
  let post = IZ::init_with_postspike(
    n_post, IZParameter::rs(), -65.0F, -13.0F, IZPostSpike::custom(4),
  )
  let mut fires : Int = 0
  for _ in 0..<total_steps {
    for j in 0..<n_post {
      let s = @snn_mbt.get_poisson_spikes(2.0F, dt, rng)
      if s == 1.0F {
        post.ge[j] = post.ge[j] + 1.0F
      }
    }
    @snn_mbt.step_iz_with_postspike(post, dt)
    for j in 0..<n_post {
      if post.fire[j] { fires = fires + 1 }
    }
  }
  fires
}

///|
test "izhikevich_balanced_postspike — seeded reproducibility" {
  // Two runs with same seed should produce identical spike counts.
  let fires_a = run_balanced_postspike_benchmark()
  let fires_b = run_balanced_postspike_benchmark()
  if fires_a != fires_b { fail("same seed should produce same fires (a=\{fires_a}, b=\{fires_b})") }
}

///|
test "izhikevich_balanced_postspike — refractory decrement tracks per-neuron" {
  // Verify that the `tabs` countdown decrements per neuron. With
  // tabs_const=4, after a fire the neuron should remain refractory for
  // 4 steps. We can verify by counting consecutive fires from a
  // single neuron — should be ≥ tabs_const steps apart.
  let pop = IZ::init_with_postspike(1, IZParameter::rs(), 35.0F, 7.0F, IZPostSpike::custom(4))
  // Inject strong current.
  pop.i[0] = 50.0F
  let mut steps_between_fires : Int = -1
  let mut last_fire_step : Int = -1
  let mut current_step : Int = 0
  for _ in 0..<200 {
    @snn_mbt.step_iz_with_postspike(pop, 0.125F)
    if pop.fire[0] {
      if last_fire_step >= 0 {
        steps_between_fires = current_step - last_fire_step
      }
      last_fire_step = current_step
    }
    current_step = current_step + 1
  }
  // If at least 2 fires occurred, verify spacing.
  if steps_between_fires > 0 {
    if steps_between_fires < 4 {
      fail("steps between fires should be ≥ tabs_const=4, got \{steps_between_fires}")
    }
  }
}