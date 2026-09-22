// adex_net.mbt — port of AdEx_net.jl (simplified).
//
// Original Julia (AdEx_net.jl):
//   E = SNN.Population(SNN.AdExParameter(El=-50mV), N=800)
//   I = SNN.Population(SNN.IFParameter(), N=200)
//   EE = SpikingSynapse(E, E, :he; conn=(μ=7,  p=0.02))
//   EI = SpikingSynapse(E, I, :ge; conn=(μ=30, p=0.02))
//   IE = SpikingSynapse(I, E, :hi; conn=(μ=50, p=0.02))
//   II = SpikingSynapse(I, I, :gi; conn=(μ=10, p=0.02))
//   sim!(model, 4s)
//
// MoonBit (v0.5.0): a tiny AdEx-only network with tonic current drive.
//   E = 8 AdEx neurons (default params; El=-70.6mV)
//   EE = SpikingSynapseAdEx(E, E, :he; μ=7, p=0.5)
//   Constant 1000 pA current → tonic spiking
//   1s simulation, AdExModel loop.
//
// Note: our AdExSpikingSynapse routes :he → glu (same as :ge) since
// the simplified DoubleExp model doesn't differentiate receptor
// rise times. The Julia :he route uses τre=1ms; we use the default
// tre=1.0F which matches.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 8
  let param = @snn_mbt.AdExParameter::new()
  let e_pop = @snn_mbt.AdEx::new(ne, param, rng)

  // Tonic drive: 1000 pA per cell. With AdEx R=0.025, R*I = 25 mV
  // which exceeds the leak at vt=-50 (about 20 mV), so cells fire.
  // (Set directly via i[] since CurrentStimulusIF targets IF, not AdEx.)
  for k in 0..<ne {
    e_pop.i[k] = 1000.0F
  }

  // Random connectivity within the AdEx population.
  let ee = @snn_mbt.SpikingSynapseAdEx::random(e_pop, e_pop, "he", 7.0F, 0.0F, 0.5F, rng)

  println("adex_net.mbt: network built")
  println("  E: " + ne.to_string() + " AdEx neurons")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  tonic drive: 1000 pA per cell")

  // Build the AdExModel and run for 1s.
  let model : @snn_mbt.AdExModel = {
    pops: [e_pop],
    conns: [ee],
    monitors: [
      @snn_mbt.MonitorAdEx::new_v(e_pop, 0),
    ],
  }
  @snn_mbt.adex_sim_for(model, 1000.0F * @snn_mbt.ms)

  // Look at the v trace from the monitor.
  println("adex_net.mbt: simulation done (1s)")
  println("  E[0] v (final) = " + e_pop.v[0].to_string())
  println("  E[0] w (final) = " + e_pop.w[0].to_string())
  println("  E[0] fire = " + e_pop.fire[0].to_string())
  // Look at v range across the trace.
  let m = model.monitors[0]
  let mut vmin = 1000.0F
  let mut vmax = -1000.0F
  for v in m.data {
    if v < vmin { vmin = v }
    if v > vmax { vmax = v }
  }
  println("  E[0] v range: [" + vmin.to_string() + ", " + vmax.to_string() + "]")
  println("  recorded v samples: " + m.data.length().to_string())
}