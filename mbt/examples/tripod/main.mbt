// tripod.mbt — minimal Tripod multi-compartment example.
//
// Julia reference (SpikingNeuralNetworks.jl/examples/experiments/
// tripod_soma.jl, simplified):
//   E = TripodHet(N = 1, ..., param = AdExSoma(El=-55mV))
//   pre = (fire = falses(1), N = 1)
//   w = 20 * ones(1, 1)
//   projection_exc_soma = CompartmentSynapse(pre, E, :s, :he, w=w)
//   sim!([E], projections; duration=1000)
//
// MoonBit (v0.10.14):
//   - 1 Tripod neuron (AdEx soma + 2 passive dendrites).
//   - Manual spike routing from a "pre" trigger into the soma glu buffer.
//   - 1s sim, prints final v_s/v_d1/v_d2 + spike count.
//
// This is the foundational Tripod example; the full Julia version
// uses TripodHet with NMDA multi-receptor synapses, which we haven't
// built yet. This version exercises single-exp synapses on both the
// soma and both dendrites.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // 1 Tripod neuron with default AdEx soma + 2 default dendrites.
  let soma_param = @snn_mbt.AdExParameter::new()
  let pop = @snn_mbt.Tripod::new(1, soma_param, rng)

  // Simulate for 1s.
  let total_steps : Int = (1000.0F / dt).to_int()
  let mut soma_spikes = 0
  for _ in 0..<total_steps {
    // 1. Inject a small constant current to drive the soma.
    pop.i_s[0] = 200.0F
    pop.i_d1[0] = 100.0F
    pop.i_d2[0] = -50.0F  // slight hyperpolarization on d2
    // 2. Step Tripod (synapses → currents → Heun → spike detect).
    @snn_mbt.step_tripod(pop, dt)
    if pop.fire[0] {
      soma_spikes = soma_spikes + 1
    }
  }

  println("tripod.mbt: 1 Tripod neuron (AdEx soma + 2 dendrites), 1s sim")
  println("  soma_param: AdExParameter default (Vr=-70.6mV, Vt=-50mV)")
  println("  dendrites: default (El=-70.6mV, C=10pF, gax=10nS, gm=1nS)")
  println("  drive: I_s=200pA, I_d1=100pA, I_d2=-50pA")
  println("tripod.mbt: simulation done (1s)")
  println("  v_s[0] final = " + pop.v_s[0].to_string())
  println("  v_d1[0] final = " + pop.v_d1[0].to_string())
  println("  v_d2[0] final = " + pop.v_d2[0].to_string())
  println("  soma spikes = " + soma_spikes.to_string())
  println("")
  println("Note: the Julia tripod_soma.jl uses TripodHet with NMDA")
  println("  multi-receptor synapses (4-receptor AMPA/NMDA/GABAa/GABAb)")
  println("  which we haven't built yet. This example uses the")
  println("  simpler single-exp synapses on all compartments.")
}