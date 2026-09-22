// poisson_layer.mbt — port of SpikingNeuralNetworks.jl/test/stim/poisson_layer.jl.
//
// Julia test reference (simplified):
//   E = IF(N=3200, El=-49mV)
//   param = PoissonLayer(rate=2Hz, N=10, active=[false])
//   conn = (p=0.2, μ=2.0, σ=1.0, dist=:Normal, rule=:Fixed)
//   stim = Stimulus(param, E, :ge, conn=conn)
//   sim!(model; duration=1s)
//
// MoonBit (v0.10.21):
//   - E = IF(N=200, El=-49mV). Smaller than Julia's 3200 to keep runtime
//     reasonable for the example; PoissonLayer semantics are identical.
//   - PoissonLayer: 10 independent Poisson sources at 2 Hz, each
//     connecting to all 200 post neurons with p=0.2, μ=2.0 nS,
//     σ=1.0 nS, Normal weight, Fixed (no-plasticity) rule.
//   - Drives the IF population through the manual sim loop
//     (stimulate_layer → step_synapses → synaptic_current →
//     step_neuron) for 1 second at dt=0.125 ms.
//   - Reports total spike count and per-neuron mean firing rate.
//
// Bit-exact note: Poisson sample sequences differ from Julia's
// Distributions.jl. Expected per-step conductance (μ × rate × dt)
// and steady-state firing rate match qualitatively.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // Build the post-synaptic IF population (El = -49 mV per Julia test).
  // Use IFParameter::custom(tm, vt, vr, el, r) with el=-49 mV.
  let if_param = @snn_mbt.IFParameter::custom(20.0F, -50.0F, -60.0F, -49.0F, 1.0F)
  let n_post = 200
  let post = @snn_mbt.IF::new(n_post, if_param, rng)

  // PoissonLayer: 10 sources, 2 Hz each, Normal weights (μ=2.0, σ=1.0),
  // p=0.2 connectivity, all active.
  let active : Array[Bool] = {
    let a : Array[Bool] = []
    for _ in 0..<10 {
      a.push(true)
    }
    a
  }
  let param = @snn_mbt.PoissonLayer::with_conn(
    2.0F * @snn_mbt.hz, // 2 Hz (converted via hz_unit = 0.001)
    10, // n_sources
    active, // all active
    2.0F, // μ
    1.0F, // σ
    0.2F, // p (connection probability)
    "Normal", // weight distribution
    "Fixed", // rule
  )
  let stim = @snn_mbt.PoissonLayerStimulus::new(param, post, "ge", rng)

  // Print initial state.
  println("poisson_layer.mbt: 10 Poisson sources @ 2 Hz → 200 IF")
  println("  weight dist: Normal(μ=2.0, σ=1.0), p=0.2, rule=Fixed")
  println("  total connection slots = " + (n_post * 10).to_string())
  let mut connected = 0
  for k in 0..<stim.connectivity.length() {
    if stim.connectivity[k] {
      connected = connected + 1
    }
  }
  let expected_conn = (Float::from_int(n_post) * 10.0F * 0.2F).to_int()
  println("  actual connections = " + connected.to_string())
  println("  expected ≈ " + expected_conn.to_string() + " (200 × 10 × 0.2)")

  // Drive the IF population for 1 second via the manual sim loop.
  // There are no SpikingSynapses, so we don't need deliver/forward;
  // only stimulate → step_synapses → synaptic_current → step_neuron.
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  let mut total_spikes = 0
  for step in 0..<steps {
    let _ = step
    @snn_mbt.stimulate_layer(stim, 0.0F, dt)
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
  println("poisson_layer.mbt: simulation done (1s, dt=0.125ms)")
  println("  total spikes = " + total_spikes.to_string())
  println("  mean firing rate = " + mean_rate.to_string() + " Hz per neuron")
  println("")
  println("Note: with El=-49 mV (only 1 mV below vt=-50 mV), the IF")
  println("  population is highly excitable, so even the modest 2 Hz")
  println("  PoissonLayer input with μ=2.0 nS Normal weights produces")
  println("  substantial firing. Lower μ or higher El would suppress it.")
}
