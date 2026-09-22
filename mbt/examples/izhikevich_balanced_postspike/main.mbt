// izhikevich_balanced_postspike.mbt — IZ + balanced stimulus + postspike.
//
// Combines:
//   - IZ RS (regular-spiking) neurons
//   - IZPostSpike with tabs_const=4 (~0.5 ms refractory)
//   - 1 kHz Bernoulli Poisson drive via `get_poisson_spikes` (v0.10.95)
//
// Demonstrates how absolute refractory shapes the E/I balance:
// without refractory, an IZ neuron's firing rate under Poisson drive
// is essentially unbounded; with refractory, the rate is bounded by
// 1/(tabs_const * dt) Hz ≈ 2 kHz (for tabs_const=4, dt=0.125ms).
//
// Run: `moon run examples/izhikevich_balanced_postspike`

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F
  let rate_khz = 1.0F  // 1 kHz Poisson drive

  // 100 RS IZ neurons with refractory tabs_const=4.
  let postspike = @snn_mbt.IZPostSpike::custom(4)
  let n_post = 100
  let post = @snn_mbt.IZ::init_with_postspike(
    n_post, @snn_mbt.IZParameter::rs(), -65.0F, -13.0F, postspike,
  )

  println("izhikevich_balanced_postspike.mbt: RS IZ (100) + 1 kHz Bernoulli")
  println("  + tabs_const=4 refractory (~0.5 ms)")
  println("  N = " + n_post.to_string())
  println("  rate = 1.0 kHz, dt = 0.125 ms (per-step prob = " +
    (rate_khz * dt).to_string() + ")")

  // Simulate for 1 second.
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  let mut total_spikes = 0
  let mut total_ge_samples : Float = 0.0F
  for _step in 0..<steps {
    let _ = _step
    // Bernoulli injection into ge (per-neuron).
    for j in 0..<n_post {
      let s = @snn_mbt.get_poisson_spikes(rate_khz, dt, rng)
      if s == 1.0F {
        post.ge[j] = post.ge[j] + 1.0F
      }
    }
    total_ge_samples = total_ge_samples +
      Float::from_int(n_post) * rate_khz * dt
    @snn_mbt.step_iz_with_postspike(post, dt)
    for j in 0..<n_post {
      if post.fire[j] {
        total_spikes = total_spikes + 1
      }
    }
  }
  let mean_rate = Float::from_int(total_spikes) *
    1000.0F / duration_ms / Float::from_int(n_post)
  let expected_total_inputs = total_ge_samples
  let input_rate = expected_total_inputs *
    1000.0F / duration_ms / Float::from_int(n_post)

  println("izhikevich_balanced_postspike.mbt: simulation done (1s, dt=0.125ms)")
  println("  total spikes = " + total_spikes.to_string())
  println("  mean firing rate = " + mean_rate.to_string() + " Hz per neuron")
  println("  mean input rate = " + input_rate.to_string() +
    " Hz per neuron (≈1kHz as expected)")
  println("")
  println("Note: with tabs_const=4 (~0.5 ms), the maximum possible firing")
  println("  rate is 1/(4*0.125 ms) = 2 kHz. Spike-frequency adaptation")
  println("  on IZ RS neurons further reduces the effective rate (~30-50 Hz).")
}