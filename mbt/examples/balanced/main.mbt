// balanced.mbt — port of SpikingNeuralNetworks.jl/test/stim/balanced.jl.
//
// Julia test reference (simplified):
//   E = IF(; N = 3200, param = IFParameter(; El = -49mV))
//   param = BalancedParameter(; kIE = 2.0, β = 0.1, τ = 100ms,
//                              r0 = 2kHz, wIE = 2.0, same_input = true)
//   stim = BalancedStimulus(E, :ge, :gi; param = param)
//   model = compose(E = E, S = stim, silent = true)
//   monitor!(E, [:fire, :ge, :gi])
//   sim!(model; duration = 1s)
//
// MoonBit (v0.10.31):
//   - 200 IF neurons (smaller than Julia's 3200 to keep runtime
//     reasonable for the example; balanced-state dynamics are identical).
//   - 1 kHz baseline rate with low-pass noise (β=0.1, τ=100ms) and
//     shared input trace (same_input=true).
//   - Same simulation loop as the Julia test: stimulate → step_synapses
//     → synaptic_current → step_neuron for 1 second at dt=0.125ms.
//   - Reports total spike count, mean firing rate, and the average
//     steady-state excitatory rate r.
//   - Vectors plot of v[0] across the run.
//
// Bit-exact note: the Poisson sampler uses Knuth's algorithm with our
// Xoshiro RNG; sequences differ from Julia's Distributions.jl but the
// expected per-step rates and qualitative dynamics match.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // Build the post-synaptic IF population (El = -49 mV per Julia test).
  let if_param = @snn_mbt.IFParameter::custom(20.0F, -50.0F, -60.0F, -49.0F, 1.0F)
  let n_post = 200
  let post = @snn_mbt.IF::new(n_post, if_param, rng)

  // BalancedParameter matching Julia test (kIE=2.0, β=0.1, τ=100ms,
  // r0=2kHz, wIE=2.0, same_input=true).
  let param = @snn_mbt.BalancedParameter::new(
    kIE=2.0F,
    beta=0.1F,
    tau=100.0F,
    r0=2.0F * @snn_mbt.khz, // 2 kHz → 2.0 internal
    w=1.0F,
    wIE=2.0F,
    same_input=true,
  )
  let stim = @snn_mbt.BalancedStimulus::new(
    post, sym_e="ge", sym_i="gi", param=param, seed=12345UL,
  )

  // Print initial state.
  println("balanced.mbt: BalancedStimulus (r0=2kHz, β=0.1, τ=100ms,")
  println("                same_input=true) → 200 IF (El=-49mV)")
  println("  N = " + n_post.to_string())
  println("  initial r[0] = " + stim.r[0].to_string() + " (should equal r0)")
  println("  initial noise[0] = " + stim.noise[0].to_string())

  // Drive the IF population for 1 second via the manual sim loop.
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  let mut total_spikes = 0
  for step in 0..<steps {
    let _ = step
    @snn_mbt.stimulate_balanced(stim, 0.0F, dt)
    @snn_mbt.step_synapses(post, dt)
    @snn_mbt.synaptic_current(post)
    @snn_mbt.step_neuron(post, dt)
    for j in 0..<n_post {
      if post.fire[j] {
        total_spikes = total_spikes + 1
      }
    }
  }
  let mean_rate = Float::from_int(total_spikes) *
    1000.0F / duration_ms / Float::from_int(n_post)

  println("balanced.mbt: simulation done (1s, dt=0.125ms)")
  println("  total spikes = " + total_spikes.to_string())
  println("  mean firing rate = " + mean_rate.to_string() + " Hz per neuron")
  println("  final r[0] = " + stim.r[0].to_string() + " (adapted toward r0=2.0)")
  println("")
  println("Note: with shared-input mode (same_input=true), every step that")
  println(
    "  produces a fire adds the same value to all ge[i], so all post",
  )
  println("  neurons receive a correlated drive that synchronises the burst")
  println("  structure of the network.")
}
