// izhikevich_balanced_postspike.mbt — IZ + balanced E/I + postspike refractory.
//
// Combines:
//   - 100 IZ RS (regular-spiking) excitatory neurons with refractory
//   - 25 IZ FS (fast-spiking) inhibitory neurons with refractory
//   - Both populations receive 1 kHz Bernoulli Poisson drive into :ge/:gi
//   - I → E cross-connections via SpikingSynapseIZ (inhibitory feedback)
//
// Demonstrates how absolute refractory + inhibitory feedback shape the
// E/I balance: without refractory, IZ firing under Poisson drive is
// essentially unbounded; with refractory (tabs_const=4 ≈ 0.5 ms), the
// max firing rate is bounded by 1/(tabs_const * dt) Hz ≈ 2 kHz.
//
// Run: `moon run examples/izhikevich_balanced_postspike`

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F
  let rate_khz = 1.0F // 1 kHz Poisson drive for E and I

  // Excitatory population: 100 RS IZ neurons with refractory.
  let n_e = 100
  let e_pop = @snn_mbt.IZ::init_with_postspike(
    n_e, @snn_mbt.IZParameter::rs(), -65.0F, -13.0F, @snn_mbt.IZPostSpike::custom(4),
  )

  // Inhibitory population: 25 FS IZ neurons with refractory.
  let n_i = 25
  let i_pop = @snn_mbt.IZ::init_with_postspike(
    n_i, @snn_mbt.IZParameter::fs(), -65.0F, -13.0F, @snn_mbt.IZPostSpike::custom(4),
  )

  println("izhikevich_balanced_postspike.mbt: E/I IZ (100E + 25I) + 1 kHz Poisson")
  println("  tabs_const=4 refractory (~0.5 ms)")
  println("  N_E = " + n_e.to_string() + ", N_I = " + n_i.to_string())
  println("  rate = 1.0 kHz, dt = 0.125 ms (per-step prob = " +
    (rate_khz * dt).to_string() + ")")

  // Simulate for 1 second. Per-step, inject Poisson spikes into
  // both populations' ge/gi fields, then step.
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  let mut total_e_spikes = 0
  let mut total_i_spikes = 0
  for _step in 0..<steps {
    // E population: excitatory drive into ge.
    for j in 0..<n_e {
      let s = @snn_mbt.get_poisson_spikes(rate_khz, dt, rng)
      if s == 1.0F {
        e_pop.ge[j] = e_pop.ge[j] + 1.0F
      }
    }
    // I population: excitatory drive into ge (FS neurons are also driven).
    for j in 0..<n_i {
      let s = @snn_mbt.get_poisson_spikes(rate_khz, dt, rng)
      if s == 1.0F {
        i_pop.ge[j] = i_pop.ge[j] + 1.0F
      }
      // FS neurons also get weak inhibitory drive for balance.
      let s2 = @snn_mbt.get_poisson_spikes(rate_khz * 0.5F, dt, rng)
      if s2 == 1.0F {
        i_pop.gi[j] = i_pop.gi[j] + 0.5F
      }
    }
    @snn_mbt.step_iz_with_postspike(e_pop, dt)
    @snn_mbt.step_iz_with_postspike(i_pop, dt)
    // Cross-talk: when an I neuron fires, send weak inhibitory to E.
    for j in 0..<n_i {
      if i_pop.fire[j] {
        // Send inhibition to a randomly chosen E neuron (deterministic).
        let target = j % n_e
        e_pop.gi[target] = e_pop.gi[target] + 0.3F
      }
    }
    for j in 0..<n_e {
      if e_pop.fire[j] {
        total_e_spikes = total_e_spikes + 1
      }
    }
    for j in 0..<n_i {
      if i_pop.fire[j] {
        total_i_spikes = total_i_spikes + 1
      }
    }
  }
  let e_mean_rate = Float::from_int(total_e_spikes) *
    1000.0F / duration_ms / Float::from_int(n_e)
  let i_mean_rate = Float::from_int(total_i_spikes) *
    1000.0F / duration_ms / Float::from_int(n_i)
  let _ = e_mean_rate
  let _ = i_mean_rate

  println("izhikevich_balanced_postspike.mbt: simulation done (1s, dt=0.125ms)")
  println("  E total spikes = " + total_e_spikes.to_string())
  println("  I total spikes = " + total_i_spikes.to_string())
  println("  E mean firing rate = " + e_mean_rate.to_string() + " Hz per neuron")
  println("  I mean firing rate = " + i_mean_rate.to_string() + " Hz per neuron")
  println("")
  println("Note: with tabs_const=4 (~0.5 ms), the maximum possible firing")
  println("  rate is 1/(4*0.125 ms) = 2 kHz. Spike-frequency adaptation")
  println("  on IZ RS neurons further reduces the effective rate.")
}
