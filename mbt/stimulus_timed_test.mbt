// Tests for SpikeTimeStimulus.

///|
test "SpikeTimeParameter::new sorts by spike time" {
  // Input is unsorted.
  let times : Array[Float] = [300.0F, 100.0F, 500.0F, 200.0F]
  let neurons : Array[Int] = [3, 1, 4, 2]
  let p = SpikeTimeParameter::new(times, neurons)
  // After sort: times ascending, neurons re-shuffled to match.
  assert_eq(p.spiketimes[0], 100.0F)
  assert_eq(p.spiketimes[1], 200.0F)
  assert_eq(p.spiketimes[2], 300.0F)
  assert_eq(p.spiketimes[3], 500.0F)
  assert_eq(p.neurons[0], 1)
  assert_eq(p.neurons[1], 2)
  assert_eq(p.neurons[2], 3)
  assert_eq(p.neurons[3], 4)
}

///|
test "SpikeTimeParameter::new keeps already-sorted input" {
  let times : Array[Float] = [50.0F, 100.0F, 200.0F]
  let neurons : Array[Int] = [0, 1, 2]
  let p = SpikeTimeParameter::new(times, neurons)
  assert_eq(p.spiketimes[0], 50.0F)
  assert_eq(p.spiketimes[2], 200.0F)
  assert_eq(p.neurons[0], 0)
  assert_eq(p.neurons[2], 2)
}

///|
test "stimulate_spiketime deposits weight at each spike time" {
  let rng = Xoshiro::default()
  let p = IF::new(3, IFParameter::new(), rng)
  let times : Array[Float] = [100.0F, 200.0F, 300.0F]
  let neurons : Array[Int] = [0, 1, 2]
  let s = SpikeTimeStimulus::new(p, "ge", times, neurons)
  // Before any step, g[0] should be 0.
  assert_eq(p.glu[0], 0.0F)
  // Step 1 (t=0.125): no spike yet (next spike at 100).
  stimulate_spiketime(s, 0.125F, 1.0F)
  assert_eq(p.glu[0], 0.0F)
  // Step at t=100: spike 0 fires, deposits 1.0 into glu[0].
  stimulate_spiketime(s, 100.0F, 1.0F)
  assert_eq(p.glu[0], 1.0F)
  // Step at t=200: spike 1 fires.
  stimulate_spiketime(s, 200.0F, 1.0F)
  assert_eq(p.glu[1], 1.0F)
  // Step at t=500: spike 2 fires; also nothing extra for spike 0/1.
  stimulate_spiketime(s, 500.0F, 1.0F)
  assert_eq(p.glu[2], 1.0F)
}

///|
test "stimulate_spiketime handles multiple spikes in one step" {
  // If the simulation jumps from t=0 directly to t=250, both spike 0
  // (at 100) and spike 1 (at 200) should fire — multiple spikes per step.
  let rng = Xoshiro::default()
  let p = IF::new(2, IFParameter::new(), rng)
  let times : Array[Float] = [100.0F, 200.0F, 500.0F]
  let neurons : Array[Int] = [0, 1, 0]
  let s = SpikeTimeStimulus::new(p, "ge", times, neurons)
  stimulate_spiketime(s, 250.0F, 2.0F)
  // Both spike 0 (at 100) and spike 1 (at 200) fire, each depositing 2.0.
  // glu[0] = 2.0 (spike 0), glu[1] = 2.0 (spike 1).
  // Spike 2 (at 500) hasn't fired.
  assert_eq(p.glu[0], 2.0F)
  assert_eq(p.glu[1], 2.0F)
}