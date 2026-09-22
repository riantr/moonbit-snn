// iz_net.mbt — port of Izikievich_net.jl (simplified).
//
// Original Julia:
//   Ne = 800; Ni = 200
//   E = IZ(; a=0.02, b=0.2, c=-65, d=8)
//   I = IZ(; a=0.1,  b=0.2, c=-65, d=2)
//   EE = SpikingSynapse(E, E, :v; conn=(μ=0.05,  p=0.8))
//   EI = SpikingSynapse(E, I, :v; conn=(μ=0.5,   p=0.8))
//   IE = SpikingSynapse(I, E, :v; conn=(μ=-1.0,  p=0.8))
//   II = SpikingSynapse(I, I, :v; conn=(μ=-1.0,  p=0.8))
//   for t = 1:1000; E.I .= 5randn(Ne); I.I .= 2randn(Ni); sim!(model, 1ms); end
//
// MoonBit (v0.4.0): small version (16 E + 4 I) for fast iteration.
// Izhikevich uses `:v` as the synaptically-coupled variable (not :ge/:gi),
// so this example focuses on the noisy-current aspect of the original.
// `SpikingSynapse` is IF-specific in v0.4.0; the IZ coupling path is
// the v0.5.0 work item.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 16
  let ni = 4
  // RS (regular spiking) for E, FS (fast spiking) for I.
  let e_param = @snn_mbt.IZParameter::rs()
  let e_pop = @snn_mbt.IZ::new(ne, e_param, rng)
  let i_param = @snn_mbt.IZParameter::fs()
  let i_pop = @snn_mbt.IZ::new(ni, i_param, rng)

  // Build E-E and I-E SpikingSynapses with :ge and :gi routes.
  let ee = @snn_mbt.SpikingSynapseIZ::random(e_pop, e_pop, "ge", 0.05F, 0.0F, 0.8F, rng)
  let ei = @snn_mbt.SpikingSynapseIZ::random(e_pop, i_pop, "ge", 0.5F, 0.0F, 0.8F, rng)
  let ie = @snn_mbt.SpikingSynapseIZ::random(i_pop, e_pop, "gi", 1.0F, 0.0F, 0.8F, rng)
  let ii = @snn_mbt.SpikingSynapseIZ::random(i_pop, i_pop, "gi", 1.0F, 0.0F, 0.8F, rng)

  println("iz_net.mbt: network built (v0.7.1: with IZ SpikingSynapses)")
  println("  E: " + ne.to_string() + " IZ neurons, I: " + ni.to_string() + " IZ neurons")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  EI connections: " + ei.matrix.nnz().to_string())
  println("  IE connections: " + ie.matrix.nnz().to_string())
  println("  II connections: " + ii.matrix.nnz().to_string())

  // 1000 steps of 1 ms each, with random current per cell.
  let dt = 0.125F
  let mut total_e_spikes = 0
  let mut total_i_spikes = 0
  for _ in 0..<1000 {
    // Inject Gaussian noise: 5x scale for E, 2x scale for I.
    for k in 0..<ne {
      let (z1, _) = @snn_mbt.box_muller(rng)
      e_pop.i[k] = 5.0F * Float::from_double(z1)
    }
    for k in 0..<ni {
      let (z1, _) = @snn_mbt.box_muller(rng)
      i_pop.i[k] = 2.0F * Float::from_double(z1)
    }
    // 1ms = 8 steps at dt=0.125F
    for _ in 0..<8 {
      @snn_mbt.forward_iz_synapse(ee)
      @snn_mbt.forward_iz_synapse(ei)
      @snn_mbt.forward_iz_synapse(ie)
      @snn_mbt.forward_iz_synapse(ii)
      @snn_mbt.step_iz(e_pop, dt)
      @snn_mbt.step_iz(i_pop, dt)
    }
    // Count spikes in the last step.
    for f in e_pop.fire {
      if f { total_e_spikes = total_e_spikes + 1 }
    }
    for f in i_pop.fire {
      if f { total_i_spikes = total_i_spikes + 1 }
    }
  }
  println("iz_net.mbt: simulation done (1s)")
  println("  E v[0] = " + e_pop.v[0].to_string())
  println("  I v[0] = " + i_pop.v[0].to_string())
  println("  E total spikes across all steps: " + total_e_spikes.to_string())
  println("  I total spikes across all steps: " + total_i_spikes.to_string())
}
