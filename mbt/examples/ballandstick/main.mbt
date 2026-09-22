// ballandstick.mbt — minimal BallAndStick multi-compartment example.
//
// Julia reference (SpikingNeuralNetworks.jl/test/pop/ballandstick.jl,
// simplified): one ball-and-stick neuron with manual soma and
// dendrite current injection, observe v_s / v_d over time.
//
// MoonBit (v0.10.16):
//   - 1 BallAndStick neuron (AdEx soma + 1 passive dendrite).
//   - Manual current injection: I_s = 1500 pA (strong, drives spike),
//     I_d = 0 pA (passive, follows soma via axial conductance).
//   - 1 s sim at dt = 0.125 ms (matches the standard SNN.jl sim grid).
//   - Reports final v_s / v_d / w_s + spike count.
//
// Why these drive currents? Default AdEx soma (Vr=-70.6, Vt=-50,
// tau_w=144, a=4, b=0.0805) needs sustained current to cross threshold
// for tonic firing. 1500 pA is well above rheobase for default
// parameters. I_d = 0 means the dendrite just leaks toward El and
// receives axial coupling from the spiking soma (visible as subthreshold
// depolarizations in v_d).

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // 1 BallAndStick neuron with default AdEx soma + default dendrite.
  let soma_param = @snn_mbt.AdExParameter::new()
  let pop = @snn_mbt.BallAndStick::new(1, soma_param, rng)

  // Simulate for 1s.
  let total_steps : Int = (1000.0F / dt).to_int()
  let mut soma_spikes = 0
  let mut v_s_min = pop.v_s[0]
  let mut v_d_max = pop.v_d[0]
  for _ in 0..<total_steps {
    // 1. Inject constant current into soma, none into dendrite.
    pop.i_s[0] = 1500.0F
    pop.i_d[0] = 0.0F
    // 2. Step BallAndStick (synapses → currents → Heun → spike detect).
    @snn_mbt.step_ballandstick(pop, dt)
    if pop.fire[0] {
      soma_spikes = soma_spikes + 1
    }
    // Track envelope.
    if pop.v_s[0] < v_s_min {
      v_s_min = pop.v_s[0]
    }
    if pop.v_d[0] > v_d_max {
      v_d_max = pop.v_d[0]
    }
  }

  println("ballandstick.mbt: 1 BallAndStick neuron (AdEx soma + 1 dendrite), 1s sim")
  println("  soma_param: AdExParameter default (Vr=-70.6mV, Vt=-50mV, tau_w=144ms, a=4nS, b=0.0805pA)")
  println("  dendrite: default (El=-70.6mV, C=10pF, gax=10nS, gm=1nS)")
  println("  drive: I_s=1500pA (above rheobase), I_d=0pA (passive)")
  println("ballandstick.mbt: simulation done (1s, dt=0.125ms)")
  println("  v_s[0] final = " + pop.v_s[0].to_string() + " (mV)")
  println("  v_s[0] min   = " + v_s_min.to_string() + " (mV; should be <= Vr=-70.6)")
  println("  v_d[0] final = " + pop.v_d[0].to_string() + " (mV)")
  println("  v_d[0] max   = " + v_d_max.to_string() + " (mV; tracks soma via axial)")
  println("  w_s[0] final = " + pop.w_s[0].to_string() + " (pA)")
  println("  soma spikes = " + soma_spikes.to_string())
  println("")
  println("Note: this exercises the BallAndStick neuron built in v0.10.13")
  println("  (AdEx soma + 1 passive Dendrite with Heun predictor-corrector).")
  println("  Spike count with default parameters + 1500pA tonic drive should")
  println("  be a regular tonic-spiking rate (target > 5 Hz for default a=4).")
}