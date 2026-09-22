// izhikevich_balanced.mbt — IZ neuron driven by a Poisson spike train.
//
// Combines:
//   - IZ (regular-spiking RS) neuron from neuron_iz.mbt
//   - Bernoulli sampling from analysis_protocols.mbt
//     (`get_poisson_spikes(rate, dt, rng)`) for rate-driven Poisson
//     injection directly into the IZ `glu` field.
//
// Each timestep, every IZ neuron receives a 1 kHz Bernoulli spike
// (probability rate*dt) into its glu field, mimicking a background
// excitatory drive. With dt=0.125ms, lambda=0.125 → ~12.5% per-step
// spike probability.
//
// Note: BalancedStimulus (v0.10.31) is IF-specific. To drive IZ with
// a balanced E/I input, we'd need a generic PoissonStimulus dispatch on
// the `glu`/`gaba` fields — left as a follow-up since IZ's
// `step_synapses` and `synaptic_current` aren't yet ported. This example
// demonstrates the excitatory-only case (still useful for showing
// spike-frequency adaptation in IZ).

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F
  let rate_khz = 1.0F  // 1 kHz Poisson drive

  // 200 RS IZ neurons.
  let param = @snn_mbt.IZParameter::rs()
  let n_post = 200
  let post = @snn_mbt.IZ::new(n_post, param, rng)

  println("izhikevich_balanced.mbt: RS IZ (200) + Poisson drive (1kHz)")
  println("  N = " + n_post.to_string())
  println("  rate = 1.0 kHz, dt = 0.125ms (per-step prob = " +
    (rate_khz * dt).to_string() + ")")

  // Simulate for 1 second.
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  let mut total_spikes = 0
  for step in 0..<steps {
    let _ = step
    // Bernoulli injection into ge (per-neuron). IZ uses ge/gi directly
    // (no separate glu/gaba buffers — `step_iz` consumes ge/gi as
    // 2-state double-exp synaptic conductances).
    for j in 0..<n_post {
      let s = @snn_mbt.get_poisson_spikes(rate_khz, dt, rng)
      if s == 1.0F {
        post.ge[j] = post.ge[j] + 1.0F
      }
    }
    // Single step of IZ dynamics.
    @snn_mbt.step_iz(post, dt)
    for j in 0..<n_post {
      if post.fire[j] {
        total_spikes = total_spikes + 1
      }
    }
  }
  let mean_rate = Float::from_int(total_spikes) *
    1000.0F / duration_ms / Float::from_int(n_post)

  println("izhikevich_balanced.mbt: simulation done (1s, dt=0.125ms)")
  println("  total spikes = " + total_spikes.to_string())
  println("  mean firing rate = " + mean_rate.to_string() + " Hz per neuron")
  println("")
  println("Note: regular-spiking IZ neurons have spike-frequency adaptation")
  println("  (the slow `u` variable that builds up during sustained firing),")
  println("  so the steady-state rate under constant Poisson drive is far")
  println("  below the input rate (typically 5-15 Hz vs 1 kHz).")
}