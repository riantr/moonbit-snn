// afferent_response.mbt — port of afferent_response.jl (simplified).
//
// Original Julia (afferent_response.jl):
//   E = IF(Npop.E, exc); I = IF(Npop.I, inh)
//   AfferentParam = PoissonLayer(rate=20Hz, N=100)
//   afferentE = Stimulus(AfferentParam, E, :ge, conn=(p=0.1, μ=4.0))
//   afferentI = Stimulus(AfferentParam, I, :ge, conn=(p=0.1, μ=4.0))
//   E_to_E = SpikingSynapse(E, E, :ge, conn=(p=0.05, μ=2.0))
//   ... etc.
//   The Julia example sweeps νa = exp.(range(log(1), log(40), 20))
//   and plots mean firing rate vs afferent rate (transfer function).
//
// MoonBit (v0.9.1): tiny version (40 E + 10 I) with a SINGLE
// afferent rate (20Hz × scaled-down multipliers) so we can
// demonstrate the network reaches a stable firing rate under
// background drive. Sweeping the rate would require a small
// CLI-driven parameter sweep which is out of scope.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 40
  let ni = 10
  let dt = 0.125F

  // IF parameters with tonic drive to overcome El→Vt gap.
  let param = @snn_mbt.IFParameter::new()
  let e_pop = @snn_mbt.IF::new(ne, param, rng)
  let i_pop = @snn_mbt.IF::new(ni, param, rng)
  for k in 0..<ne { e_pop.i[k] = 350.0F }
  for k in 0..<ni { i_pop.i[k] = 250.0F }

  // Recurrent connectivity (Fixed-rule with p=0.05).
  let ee = @snn_mbt.SpikingSynapse::random(e_pop, e_pop, "ge",
    2.0F, 0.0F, 0.05F, rng)
  let ei = @snn_mbt.SpikingSynapse::random(e_pop, i_pop, "ge",
    2.0F, 0.0F, 0.05F, rng)
  let ie = @snn_mbt.SpikingSynapse::random(i_pop, e_pop, "gi",
    10.0F, 0.0F, 0.05F, rng)
  let ii = @snn_mbt.SpikingSynapse::random(i_pop, i_pop, "gi",
    10.0F, 0.0F, 0.05F, rng)

  // Poisson afferents at 20 Hz × 100 = 2000Hz (MoonBit internal 2.0F).
  let aff_e = @snn_mbt.PoissonStimulusIF::new(e_pop, "ge", 2.0F, rng)
  let aff_i = @snn_mbt.PoissonStimulusIF::new(i_pop, "ge", 2.0F, rng)

  // Build model.
  let fm_e = @snn_mbt.Monitor::new_fire(e_pop, 0)
  let fm_i = @snn_mbt.Monitor::new_fire(i_pop, 0)
  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(e_pop), @snn_mbt.IF_(i_pop)],
    [ee, ei, ie, ii],
    stims=[@snn_mbt.PoissonIF_(aff_e), @snn_mbt.PoissonIF_(aff_i)],
    monitors=[fm_e, fm_i],
  )

  // Simulate 1 s.
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  for _ in 0..<steps {
    @snn_mbt.step_heterogeneous(model, dt)
  }

  let e_hz = fm_e.firing_rate()
  let i_hz = fm_i.firing_rate()
  println("afferent_response.mbt: simplified afferent-driven network")
  println("  E: " + ne.to_string() + " IF neurons + 350 pA tonic")
  println("  I: " + ni.to_string() + " IF neurons + 250 pA tonic")
  println("  Poisson drive: 2000Hz on both E and I (Julia: 20Hz × 100)")
  println("afferent_response.mbt: simulation done (1s)")
  println("  E[0] Hz: " + e_hz.to_string())
  println("  I[0] Hz: " + i_hz.to_string())
}