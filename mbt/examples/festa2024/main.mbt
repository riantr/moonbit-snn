// festa2024.mbt - simplified port of Festa2024 Structured Inhibition
// network (asymmetric STDP variant). Scaled 10x down.
//
// Original Julia (Festa2024_StructuredInhibition/
//   structured_inhibition_asymmetric_STDP_network.jl):
//   Ne = 800, Ni = 200
//   pee = 0.2, pei = 0.1, pie = 0.3, pii = 0.2
//   wee = 1.0, wei = 1.0, wie = 1.8, wii = 0.3
//   stim_e = PoissonStimulus(E, :ge, rate = 50Hz*50, mu = 1.0)
//   stim_i = PoissonStimulus(I1, :ge, rate = 50Hz*40, mu = 0.2)
//   IE synapse has asymmetric STDP attached
//   4 s simulation, monitor :fire
//
// MoonBit (v0.7.9): tiny version (80 E + 20 I1), STDP on the I1->E
// synapse (auto-applied via compose layer). We don't have
// asymmetric-STDP specifically, so the Gerstner rule stands in.
//
// Note: rate normalisation. Our internal units have Hz = 0.001F,
// so 50Hz -> 50*0.001F = 0.05F. Then *50 = 2.5F (= 2500Hz internal).
// And dt = 0.125F, so lambda = rate * dt = 2.5F * 0.125F = 0.3125.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 80
  let ni = 20
  let dt = 0.125F

  // E and I1 populations.
  let e_pop = @snn_mbt.IF::new(ne, @snn_mbt.IFParameter::new(), rng)
  let i_pop = @snn_mbt.IF::new(ni, @snn_mbt.IFParameter::new(), rng)

  // Random connectivity (Bernoulli).
  let ee = @snn_mbt.SpikingSynapse::random(e_pop, e_pop, "ge",
    1.0F, 0.0F, 0.2F, rng)
  let ei = @snn_mbt.SpikingSynapse::random(e_pop, i_pop, "ge",
    1.0F, 0.0F, 0.1F, rng)
  let ie = @snn_mbt.SpikingSynapse::random(i_pop, e_pop, "gi",
    1.8F, 0.0F, 0.3F, rng)
  let ii = @snn_mbt.SpikingSynapse::random(i_pop, i_pop, "gi",
    0.3F, 0.0F, 0.2F, rng)

  // Poisson stimuli. Rate = 2500Hz for E (50Hz*50), 2000Hz for I1 (50Hz*40).
  // In internal units, 1 Hz = 0.001, so 2500Hz -> 2.5F, 2000Hz -> 2.0F.
  let stim_e = @snn_mbt.PoissonStimulusIF::new(e_pop, "ge", 2.5F, rng)
  let stim_i = @snn_mbt.PoissonStimulusIF::new(i_pop, "ge", 2.0F, rng)

  // STDP on the I1->E (inhibitory) synapse - conns[2].
  let stdp_entry = @snn_mbt.Gerstner_(@snn_mbt.STDPEntry::new(2, ni, ne))

  println("festa2024.mbt: simplified Festa2024 network")
  println("  E: " + ne.to_string() + " IF neurons (pee=0.2, wee=1.0)")
  println("  I1: " + ni.to_string() + " IF neurons (pei=0.1, pie=0.3, etc.)")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  EI connections: " + ei.matrix.nnz().to_string())
  println("  IE connections: " + ie.matrix.nnz().to_string())
  println("  II connections: " + ii.matrix.nnz().to_string())
  println("  Poisson stim: 2500Hz on E, 2000Hz on I1")
  println("  STDP auto-applied to I1->E (conns[2])")

  // Build model with fire monitors and STDP.
  let fm_e = @snn_mbt.Monitor::new_fire(e_pop, 0)
  let fm_i = @snn_mbt.Monitor::new_fire(i_pop, 0)
  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(e_pop), @snn_mbt.IF_(i_pop)],
    [ee, ei, ie, ii],
    stims=[@snn_mbt.PoissonIF_(stim_e), @snn_mbt.PoissonIF_(stim_i)],
    monitors=[fm_e, fm_i],
    stdp=[stdp_entry],
  )

  // Simulate 1 s (40x scaled down from 4s).
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  for _ in 0..<steps {
    @snn_mbt.step_heterogeneous(model, dt)
  }

  let e_hz = fm_e.firing_rate()
  let i_hz = fm_i.firing_rate()
  let e_count = fm_e.count_spikes()
  let i_count = fm_i.count_spikes()
  println("festa2024.mbt: simulation done (1s)")
  println("  E[0] spikes: " + e_count.to_string() + " (" + e_hz.to_string() + " Hz)")
  println("  I1[0] spikes: " + i_count.to_string() + " (" + i_hz.to_string() + " Hz)")
  let initial_ie_weight_sum = Float::from_int(ie.matrix.nnz()) * 1.8F
  let mut final_ie_sum = 0.0F
  let mut k = 0
  while k < ie.matrix.vals.length() {
    final_ie_sum = final_ie_sum + ie.matrix.vals[k]
    k = k + 1
  }
  println("  I1->E weight sum: " + final_ie_sum.to_string() +
    " (initial=" + initial_ie_weight_sum.to_string() + ")")
}