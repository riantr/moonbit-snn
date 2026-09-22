// hh_current.mbt — port of HH_neuron.jl (single-neuron current injection).
//
// Julia:
//   E = SNN.HH(; N = 1)
//   SNN.monitor!(E, [:v, :fire, :m, :n, :h], sr = 1kHz)
//   sim!(model, 1s)
//
// MoonBit (v0.7.0): single HH neuron with constant 10 µA/cm² current
// injected via CurrentStimulusArray (HH doesn't have a dedicated IF
// stimulus, so we use the generic array variant which writes into
// the population's `i` field).
//
// We can't use heterogeneous_sim_for because compose uses integrate_any
// which doesn't know about HH yet (v0.7.0+ work item). Run the loop
// manually.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let param = @snn_mbt.HHParameter::new()
  let pop = @snn_mbt.HH::new(1, param, rng)

  // 10 µA/cm² current (default HH unit, internally normalised to 10.0F).
  let stim = @snn_mbt.CurrentStimulusArray::new(pop.i, pop.n, 10.0F, rng)

  println("hh_current.mbt: simulation starting (1s)")
  println("  tonic drive: 10 µA/cm² per cell")

  let dt = 0.125F
  let total_steps : Int = (1000.0F / dt).to_int()  // 1 s = 8000 steps
  let mut spikes = 0
  for _ in 0..<total_steps {
    // Stimulus first (sets i[i]).
    @snn_mbt.stimulate_current_array(stim)
    // Track rising-edge of fire flag.
    let prev_fire = pop.fire[0]
    // Step HH.
    @snn_mbt.step_hh(pop, dt)
    if pop.fire[0] && !prev_fire {
      spikes = spikes + 1
    }
  }

  println("hh_current.mbt: simulation done (1s)")
  println("  v[0] = " + pop.v[0].to_string())
  println("  m[0] = " + pop.m[0].to_string())
  println("  n_gate[0] = " + pop.n_gate[0].to_string())
  println("  h[0] = " + pop.h[0].to_string())
  println("  total spikes: " + spikes.to_string())
}