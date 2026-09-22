// coba_net.mbt — port of COBA.jl (Conductance-Based network, scaled down).
//
// Original Julia (COBA.jl):
//   neuron_param = SNN.IFParameter(;
//                    τm = 20ms, R = 100MΩ, Vt = -50mV, Vr = -60mV, El = -60mV)
//   spike = SNN.PostSpike(τabs=5ms, up=0ms, At=0ms, AP_membrane=0mV)
//   synapse = SNN.SingleExpSynapse(τe=5ms, τi=10ms, E_e=0mV, E_i=-80mV)
//   E = SNN.Population(;param=neuron_param, synapse, spike, N=8000, name="Exc")
//   I = SNN.Population(;param=neuron_param, synapse, spike, N=2000, name="Inh")
//   E.I .= 150pA;  I.I .= 150pA
//   EE = SpikingSynapse(E, E, :glu; conn=(μ=60*0.27/10, p=0.02, rule=:Bernoulli),
//                       delay_dist=Normal(0.8ms, 0))
//   EI = SpikingSynapse(E, I, :glu; conn=(μ=60*0.27/10, p=0.02, rule=:Bernoulli),
//                       delay_dist=Normal(0.8ms, 0))
//   II = SpikingSynapse(I, I, :gaba; conn=(μ=20*4.5/10, p=0.02, rule=:Bernoulli),
//                       delay_dist=Normal(0.8ms, 0))
//   IE = SpikingSynapse(I, E, :gaba; conn=(μ=20*4.5/10, p=0.02, rule=:Bernoulli),
//                       delay_dist=Normal(0.8ms, 0))
//   model = SNN.compose(;E, I, EE, EI, IE, II)
//   monitor!(model.pop, [:fire])
//   sim!(model, 1s)         # with 150pA drive
//   E.I .= 0pA;  I.I .= 0pA
//   sim!(model, 5s)         # without drive
//
// MoonBit (v0.10.3): we scale populations down by 100x (80 E + 20 I).
// We use IFParameter::custom for τm=20ms, R=100MΩ (=0.1F), Vt=-50,
// Vr=-60, El=-60 (same as cuba_net). Routing :glu → "ge", :gaba →
// "gi" (semantically the same — Julia's :glu is :ge in our internal
// representation since SpikingSynapse::forward routes :ge → post.ge).
// delay_dist=Normal(0.8ms, 0) is implemented via
// `SpikingSynapse::random_with_delays` (v0.10.3+) — every connection
// has a constant 0.8ms delay; spikes scheduled for delivery instead
// of propagating in the same step.
//
// COBA vs CUBA difference: COBA uses conductance-based synaptic
// currents (`ge * (Ee - v) + gi * (Ei - v)`); our IF uses this by
// default. So COBA.jl is essentially our default behaviour with
// routing "ge"/"gi".

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 80
  let ni = 20
  let dt = 0.125F

  // E and I populations with custom IF parameters matching Julia.
  let neuron_param = @snn_mbt.IFParameter::custom(20.0F, -50.0F, -60.0F, -60.0F, 0.1F)
  let e_pop = @snn_mbt.IF::new(ne, neuron_param, rng)
  let i_pop = @snn_mbt.IF::new(ni, neuron_param, rng)

  // Apply 150 pA external current to both populations (phase 1).
  for k in 0..<ne {
    e_pop.i[k] = 150.0F
  }
  for k in 0..<ni {
    i_pop.i[k] = 150.0F
  }

  // Random connectivity within and between populations. COBA.jl uses
  // μ=60*0.27/10=1.62 for E→*, μ=20*4.5/10=9.0 for I→*. Julia's
  // SpikingSynapse applies μ * nS = conductance; we use the same
  // weight values (in MoonBit normalised units, nS=1).
  // v0.10.3: use random_with_delays to attach the Julia
  // `delay_dist=Normal(0.8ms, 0)` (constant 0.8ms per-connection).
  let ee = @snn_mbt.SpikingSynapse::random_with_delays(
    e_pop, e_pop, "ge", 1.62F, 0.0F, 0.02F, rng, 0.8F, 0.0F,
  )
  let ei = @snn_mbt.SpikingSynapse::random_with_delays(
    e_pop, i_pop, "ge", 1.62F, 0.0F, 0.02F, rng, 0.8F, 0.0F,
  )
  let ii = @snn_mbt.SpikingSynapse::random_with_delays(
    i_pop, i_pop, "gi", 9.0F, 0.0F, 0.02F, rng, 0.8F, 0.0F,
  )
  let ie = @snn_mbt.SpikingSynapse::random_with_delays(
    i_pop, e_pop, "gi", 9.0F, 0.0F, 0.02F, rng, 0.8F, 0.0F,
  )
  println("coba_net.mbt: network built")
  println("  E: " + ne.to_string() + " IF neurons")
  println("  I: " + ni.to_string() + " IF neurons")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  EI connections: " + ei.matrix.nnz().to_string())
  println("  II connections: " + ii.matrix.nnz().to_string())
  println("  IE connections: " + ie.matrix.nnz().to_string())
  println("  drive: 150 pA per cell (phase 1: 0-1s, phase 2: 1-6s = 0 pA)")

  // Build model with fire monitors.
  let model : @snn_mbt.Model = {
    pops: [e_pop, i_pop],
    conns: [ee, ei, ie, ii],
    monitors: [
      @snn_mbt.Monitor::new_fire(e_pop, 0),
      @snn_mbt.Monitor::new_fire(i_pop, 0),
    ],
  }

  // Manual sim loop: phase 1 (1s with drive) + phase 2 (5s without).
  let time = @snn_mbt.Time::new()
  @snn_mbt.set_dt(time, dt)
  let total_steps : Int = (6000.0F / dt).to_int()
  let phase_step : Int = (1000.0F / dt).to_int()

  @snn_mbt.record_zero(model)
  for step in 0..<total_steps {
    if step == phase_step {
      // Phase transition at t=1s (drive off).
      for k in 0..<ne { e_pop.i[k] = 0.0F }
      for k in 0..<ni { i_pop.i[k] = 0.0F }
    }
    @snn_mbt.step_model(model, time)
  }

  // Compute spike counts per phase.
  let rec_step = model.monitors[0].rec_step
  let half = (1000.0F / dt).to_int() + 1
  let mut e_phase1 = 0
  let mut i_phase1 = 0
  for s in 0..<half {
    if s < model.monitors[0].data.length() {
      if model.monitors[0].data[s] > 0.5F { e_phase1 = e_phase1 + 1 }
    }
    if s < model.monitors[1].data.length() {
      if model.monitors[1].data[s] > 0.5F { i_phase1 = i_phase1 + 1 }
    }
  }
  let e_total = model.monitors[0].count_spikes()
  let i_total = model.monitors[1].count_spikes()
  let _ = rec_step

  println("coba_net.mbt: simulation done (6s total)")
  println("  phase 1 (0-1s, drive on):")
  println("    E[0] spikes: " + e_phase1.to_string())
  println("    I[0] spikes: " + i_phase1.to_string())
  println("  phase 2 (1-6s, drive off):")
  println("    E[0] spikes: " + (e_total - e_phase1).to_string())
  println("    I[0] spikes: " + (i_total - i_phase1).to_string())
}