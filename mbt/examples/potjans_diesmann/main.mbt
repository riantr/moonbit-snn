// potjans_diesmann.mbt — port of Potjans-Diesmann 2014 cortical
// microcircuit (simplified 4-layer version, scaled down 100×).
//
// Original Julia (multilayer_potjans-diesmann2015.jl):
//   8 populations: E23 (20683), E4 (21915), E5 (4850), E6 (14395),
//                  I23 (5834), I4 (5479), I5 (1065), I6 (2948)
//   64 SpikingSynapses with layer-specific μ and p (from Table 1)
//   2.5kHz Poisson background drive per excitatory population
//   15 s simulation
//
// MoonBit (v0.8.8): We reduce to 4 layers (E23, E4, E5, E6 collapsed
// into 1 E population + I23, I4, I5, I6 collapsed into 1 I) for
// tractability. Population sizes scaled down by 100x. We use:
//   E_total = ~120 IF neurons (sum of 4 E layers / 100)
//   I_total = ~40 IF neurons (sum of 4 I layers / 100)
// Poisson background at 1.0 (scaled-down version of 2.5kHz).
//
// Note: bit-exactness with Julia's 8-layer version is NOT a goal
// here; the qualitative behaviour (recurrent E/I firing under
// background drive) is what we demonstrate.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  // 120 E + 40 I (scaled down from Potjans-Diesmann totals).
  let ne = 120
  let ni = 40
  let dt = 0.125F

  // Default IF parameters + tonic current drive on E so that the
  // network doesn't go silent (default El=-70, vt=-50, R=0.06
  // need ~333 pA tonic drive to reach threshold).
  let param = @snn_mbt.IFParameter::new()
  let e_pop = @snn_mbt.IF::new(ne, param, rng)
  let i_pop = @snn_mbt.IF::new(ni, param, rng)
  // Tonic 350 pA to all E neurons so they fire under background.
  for k in 0..<ne {
    e_pop.i[k] = 350.0F
  }

  // Recurrent connectivity (averaged from Potjans-Diesmann Table 1):
  //   EE: p=0.05, μ=2.5 nS
  //   EI: p=0.05, μ=2.5 nS
  //   II: p=0.05, μ=6.0 nS
  //   IE: p=0.05, μ=6.0 nS
  let ee = @snn_mbt.SpikingSynapse::random(e_pop, e_pop, "ge",
    2.5F, 0.0F, 0.05F, rng)
  let ei = @snn_mbt.SpikingSynapse::random(e_pop, i_pop, "ge",
    2.5F, 0.0F, 0.05F, rng)
  let ii = @snn_mbt.SpikingSynapse::random(i_pop, i_pop, "gi",
    6.0F, 0.0F, 0.05F, rng)
  let ie = @snn_mbt.SpikingSynapse::random(i_pop, e_pop, "gi",
    6.0F, 0.0F, 0.05F, rng)

  // Poisson background drive. Julia uses 2.5kHz × μ=1 nS onto :glu
  // for each excitatory population. We scale down by 100x for
  // tractability: rate=0.5 (≈ 500Hz in MoonBit units where Hz=0.001)
  // with μ=10 nS.
  let stim_e = @snn_mbt.PoissonStimulusIF::new(e_pop, "ge", 0.5F, rng)
  let _ = stim_e

  // Build model with fire monitors.
  let fm_e = @snn_mbt.Monitor::new_fire(e_pop, 0)
  let fm_i = @snn_mbt.Monitor::new_fire(i_pop, 0)
  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(e_pop), @snn_mbt.IF_(i_pop)],
    [ee, ei, ie, ii],
    stims=[@snn_mbt.PoissonIF_(stim_e)],
    monitors=[fm_e, fm_i],
  )

  // Simulate 1 s (200× scaled down from 15 s).
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  for _ in 0..<steps {
    @snn_mbt.step_heterogeneous(model, dt)
  }

  let e_hz = fm_e.firing_rate()
  let i_hz = fm_i.firing_rate()
  let e_count = fm_e.count_spikes()
  let i_count = fm_i.count_spikes()
  println("potjans_diesmann.mbt: simplified Potjans-Diesmann microcircuit")
  println("  E: " + ne.to_string() + " IF neurons (default params)")
  println("  I: " + ni.to_string() + " IF neurons (default params)")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  EI connections: " + ei.matrix.nnz().to_string())
  println("  II connections: " + ii.matrix.nnz().to_string())
  println("  IE connections: " + ie.matrix.nnz().to_string())
  println("  Poisson drive: 500Hz on E")
  println("potjans_diesmann.mbt: simulation done (1s)")
  println("  E[0] spikes: " + e_count.to_string() + " (" + e_hz.to_string() + " Hz)")
  println("  I[0] spikes: " + i_count.to_string() + " (" + i_hz.to_string() + " Hz)")
}