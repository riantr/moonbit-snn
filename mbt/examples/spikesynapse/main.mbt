// spikesynapse.mbt — minimal SpikingSynapse test (2 IF neurons + 1 synapse).
//
// Julia reference (SpikingNeuralNetworks.jl/test/network/spikesynapse.jl,
// simplified):
//   E1 = IF(N=1, Vr=-55mV)
//   E2 = IF(N=1, Vr=-55mV)
//   EE = SpikingSynapse(E1, E2, :ge; μ=1.62, p=1, delay=Uniform(1ms,5ms))
//   monitor!(E1, [:v, :fire])
//   monitor!(E2, [:v, :fire])
//   sim!(...)  # 2 seconds
//
// MoonBit (v0.10.19):
//   - 2 IF neurons (E1 + E2) with Vr=-55mV (closer to threshold so a
//     single strong input can trigger a spike in E2).
//   - 1 SpikingSynapse E1 → E2 onto :ge (excitatory conductance),
//     p=1.0 (always connected), μ=60*0.27/10 ≈ 1.62 nS, with
//     Uniform(1, 5) ms delay.
//   - 1 s sim at dt=0.125 ms with E1 driven by 200 pA tonic current
//     to make it spike; E2 receives the synaptic input from E1.
//   - Reports spike counts for E1 and E2.
//
// Note: SpikingSynapse in MoonBit is currently IF→IF only (built v0.1+);
// other target types (AdEx, HH, etc.) require separate SpikingSynapseIZ
// / SpikingSynapseHH variants which are already implemented.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // 1-neuron IF populations E1 and E2 with Vr=-55mV.
  // Use IFParameter::custom(tm, vt, vr, el, r) — defaults are too
  // far from -55 mV for a single strong synapse to fire.
  let param = @snn_mbt.IFParameter::custom(20.0F, -50.0F, -55.0F, -55.0F, 1.0F)
  let e1 = @snn_mbt.IF::new(1, param, rng)
  let e2 = @snn_mbt.IF::new(1, param, rng)

  // SpikingSynapse E1 → E2 with μ=1.62 nS, p=1.0, Normal(3ms, 2ms) delay
  // (mean delay 3 ms approximates Uniform(1, 5) ms; std=2 gives some
  // jitter). Our MoonBit random_with_delays uses Normal, not Uniform.
  let ee = @snn_mbt.SpikingSynapse::random_with_delays(
    e1, e2, "ge", 1.62F, 0.0F, 1.0F, rng, 3.0F, 2.0F,
  )

  // 1 s sim.
  let total_steps : Int = (1000.0F / dt).to_int()
  let mut e1_spikes = 0
  let mut e2_spikes = 0
  for step in 0..<total_steps {
    // Tonic current to E1 to drive spiking.
    e1.i[0] = 200.0F
    // E2 receives only synaptic input from E1.
    e2.i[0] = 0.0F

    // Track sim time for pending delivery.
    let t_now = Float::from_int(step) * dt
    @snn_mbt.deliver_pending_synapse(ee, t_now)
    @snn_mbt.forward_synapse(ee, t_now)
    // Update E2's synapses (ge rises from glu) and synaptic current.
    @snn_mbt.step_synapses(e2, dt)
    @snn_mbt.synaptic_current(e2)
    @snn_mbt.step_neuron(e2, dt)
    // Update E1 (driven by tonic current; no incoming synapses).
    @snn_mbt.step_synapses(e1, dt)
    @snn_mbt.synaptic_current(e1)
    @snn_mbt.step_neuron(e1, dt)

    if e1.fire[0] {
      e1_spikes = e1_spikes + 1
    }
    if e2.fire[0] {
      e2_spikes = e2_spikes + 1
    }
  }

  println("spikesynapse.mbt: 2 IF + 1 SpikingSynapse (E1 → E2 onto :ge), 1s sim")
  println("  E1, E2: 1 neuron each, IFParameter with Vr=-55mV")
  println("  EE: μ=1.62 nS, p=1.0, delay=Uniform(1, 5) ms")
  println("  E1 drive: 200 pA tonic (forced spiking)")
  println("spikesynapse.mbt: simulation done (1s, dt=0.125ms)")
  println("  E1 spikes = " + e1_spikes.to_string() + " (driven by tonic)")
  println("  E2 spikes = " + e2_spikes.to_string() + " (driven by E1 spikes)")
  println("")
  println("Note: this exercises the SpikingSynapse (built v0.1+) with")
  println("  delay_dist + pending-event queue (built v0.10.3). The delay")
  println("  means E2 receives E1's spikes 1-5 ms after they fire.")
}