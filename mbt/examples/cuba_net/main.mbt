// cuba_net.mbt — port of CUBA.jl (Current-Based network, scaled down).
//
// Original Julia (CUBA.jl):
//   neuron_param = SNN.IFParameter(;
//                    τm = 20ms, R = 100MΩ, Vt = -50mV, Vr = -60mV, El = -60mV)
//   spike = SNN.PostSpike(τabs=5ms, up=0ms, At=0ms, AP_membrane=0mV)
//   E = SNN.Population(;param=neuron_param, synapse, spike, N=8000, name="Exc")
//   I = SNN.Population(;param=neuron_param, synapse, spike, N=2000, name="Inh")
//   E.I .= 150pA;  I.I .= 150pA
//   EE = SpikingSynapse(E, E, :glu; conn=(μ=60*0.27/10, p=0.02, rule=:Bernoulli))
//   EI = SpikingSynapse(E, I, :glu; conn=(μ=60*0.27/10, p=0.02, rule=:Bernoulli))
//   II = SpikingSynapse(I, I, :gaba; conn=(μ=20*4.5/10, p=0.02, rule=:Bernoulli))
//   IE = SpikingSynapse(I, E, :gaba; conn=(μ=20*4.5/10, p=0.02, rule=:Bernoulli))
//   model = SNN.compose(;E, I, EE, EI, IE, II)
//   monitor!(model.pop, [:fire])
//   sim!(model, 5s)         # with 150pA drive
//   E.I .= 0pA;  I.I .= 0pA
//   sim!(model, 5s)         # without drive
//
// MoonBit (v0.7.5): we scale populations down by 100x (80 E + 20 I) so
// the example finishes in seconds. We also keep IF defaults (τm=15ms,
// R=0.06) rather than the Julia overrides — the network dynamics are
// qualitatively the same (150 pA drives strong spiking, then silent
// after the drive is removed). We use :ge/:gi as routing targets (the
// equivalent of Julia's :glu/:gaba) and Bernoulli rule with p=0.02.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 80
  let ni = 20
  let dt = 0.125F

  // E and I populations with custom IF parameters matching Julia:
  //   τm = 20ms, R = 100MΩ (= 0.1F in MoonBit normalised units),
  //   Vt = -50mV, Vr = -60mV, El = -60mV.
  // We need R=0.1F (Julia: 100 * mohm = 100 * 0.001 = 0.1) and El/Vr
  // close together so the 150 pA drive (R*I = 15 mV) easily pushes v
  // past Vt from El=-60 to Vt=-50.
  let neuron_param = @snn_mbt.IFParameter::custom(20.0F, -50.0F, -60.0F, -60.0F, 0.1F)
  let e_pop = @snn_mbt.IF::new(ne, neuron_param, rng)
  let i_pop = @snn_mbt.IF::new(ni, neuron_param, rng)

  // Apply 150 pA external current to both populations.
  for k in 0..<ne {
    e_pop.i[k] = 150.0F
  }
  for k in 0..<ni {
    i_pop.i[k] = 150.0F
  }

  // Random connectivity within and between populations.
  let ee = @snn_mbt.SpikingSynapse::random(e_pop, e_pop, "ge",
    1.6F, 0.0F, 0.02F, rng)
  let ei = @snn_mbt.SpikingSynapse::random(e_pop, i_pop, "ge",
    1.6F, 0.0F, 0.02F, rng)
  let ii = @snn_mbt.SpikingSynapse::random(i_pop, i_pop, "gi",
    9.0F, 0.0F, 0.02F, rng)
  let ie = @snn_mbt.SpikingSynapse::random(i_pop, e_pop, "gi",
    9.0F, 0.0F, 0.02F, rng)
  println("cuba_net.mbt: network built")
  println("  E: " + ne.to_string() + " IF neurons")
  println("  I: " + ni.to_string() + " IF neurons")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  EI connections: " + ei.matrix.nnz().to_string())
  println("  II connections: " + ii.matrix.nnz().to_string())
  println("  IE connections: " + ie.matrix.nnz().to_string())
  println("  drive: 150 pA per cell (phase 1: 0-5s, phase 2: 5-10s = 0 pA)")

  // Build model with fire monitors for both populations.
  let model : @snn_mbt.Model = {
    pops: [e_pop, i_pop],
    conns: [ee, ei, ie, ii],
    monitors: [
      @snn_mbt.Monitor::new_fire(e_pop, 0),
      @snn_mbt.Monitor::new_fire(i_pop, 0),
    ],
  }

  // Manual sim loop: 5s with drive, 5s without.
  // We can't easily inject the phase transition into sim_for, so we
  // do the loop ourselves and toggle the external current mid-run.
  let time = @snn_mbt.Time::new()
  @snn_mbt.set_dt(time, dt)
  let total_steps : Int = (10000.0F / dt).to_int()
  let phase_step : Int = (5000.0F / dt).to_int()

  // Record t=0 (phase 1, with drive).
  @snn_mbt.record_zero(model)
  for step in 0..<total_steps {
    // Phase transition at t=5s (right before step_model runs at t=5s).
    if step == phase_step {
      for k in 0..<ne { e_pop.i[k] = 0.0F }
      for k in 0..<ni { i_pop.i[k] = 0.0F }
    }
    @snn_mbt.step_model(model, time)
  }

  // Compute spike counts.
  let e_total_spikes = model.monitors[0].count_spikes()
  let i_total_spikes = model.monitors[1].count_spikes()

  // Re-run a second model just to get phase-1 (0-5s) counts. Easiest:
  // compute cumulative fire flags from the monitor data.
  // The fire monitor stores 1.0F if fired, 0.0F otherwise. Each spike
  // is a 1 sample. The first half of the trace is phase 1.
  let rec_step = model.monitors[0].rec_step
  let n_samples = model.monitors[0].data.length()
  // Half-point between phase 1 and phase 2 in monitor samples.
  // t=0 .. t=5s includes 5s/dt = 40000 steps, plus the t=0 record =
  // 40001 samples. Phase 2 is the rest.
  let half = (5000.0F / dt).to_int() + 1
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
  let _ = rec_step
  let _ = n_samples
  let e_phase2 = e_total_spikes - e_phase1
  let i_phase2 = i_total_spikes - i_phase1

  println("cuba_net.mbt: simulation done (10s total)")
  println("  phase 1 (0-5s, drive on):")
  println("    E[0] spikes: " + e_phase1.to_string())
  println("    I[0] spikes: " + i_phase1.to_string())
  println("  phase 2 (5-10s, drive off):")
  println("    E[0] spikes: " + e_phase2.to_string())
  println("    I[0] spikes: " + i_phase2.to_string())
}