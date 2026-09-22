// tripod_network.mbt — multi-population E-I Tripod network demo.
//
// Reference Julia (SpikingNeuralNetworks.jl/test/network/tripod_network.jl):
//   NE = 400; NI = 100
//   E   = Tripod()
//   I1  = IF(; N = NI÷2, param = IFParameter(τm = 7ms,  El = -55mV))
//   I2  = IF(; N = NI÷2, param = IFParameter(τm = 20ms, El = -55mV))
//   E_to_I1 = SpikingSynapse(E, I1, :ge, p = 0.2, μ = 15.0)
//   E_to_I2 = SpikingSynapse(E, I2, :ge, p = 0.2, μ = 15.0)
//   I2_to_E = CompartmentSynapse(I2, E, :d1, :hi, p = 0.2, μ = 5.0,
//                                param = iSTDPPotential(v0 = -50mV))
//   I1_to_E = CompartmentSynapse(I1, E, :s,  :hi, p = 0.2, μ = 5.0,
//                                param = iSTDPRate(r = 10Hz))
//   E_to_E_d1 = CompartmentSynapse(E, E, :d1, :he, p = 0.2, μ = 30,
//                                  param = vSTDPParameter())
//   E_to_E_d2 = CompartmentSynapse(E, E, :d2, :he, p = 0.2, μ = 30,
//                                  param = vSTDPParameter())
//   recurrent_norm_d1 = SynapseNormalization(E, [E_to_E_d1],
//                                          param = MultiplicativeNorm(τ = 100ms))
//   recurrent_norm_d2 = SynapseNormalization(E, [E_to_E_d2],
//                                          param = MultiplicativeNorm(τ = 100ms))
//   model = compose(E = E, I1 = I1, I2 = I2,
//                  E_to_I1, E_to_I2, I1_to_E, I2_to_E,
//                  E_to_E_d1, E_to_E_d2, recurrent_norm_d1, recurrent_norm_d2)
//   train!([E, I1, I2], [synapses, norms]; duration = 5000ms)
//
// MoonBit (v0.10.37):
//   - 20 Tripod (E) + 5 IF (I1) + 5 IF (I2). 100× scaled down.
//   - I1_to_E_soma, I2_to_E_d1 wired via CompartmentSynapseTripod.
//     E_to_I1, E_to_I2 routed manually (Tripod as source).
//   - E_to_E_d1, E_to_E_d2 (recurrent) SKIPPED — Tripod can't be
//     used as pre in CompartmentSynapseTripod. Could be added
//     later via a Poisson-style stimulus from Tripod.fire.
//   - Homeostatic normalization (SynapseNormalization +
//     MultiplicativeNorm) SKIPPED — not yet ported.
//   - iSTDPRate, iSTDPPotential, vSTDPParameter all available.
//   - Manual sim loop: forward → step_tripod → step_neuron (IF)
//     → STDP! updates.
//
// Bit-exact note: the Julia example uses ~5000ms train; we use 500ms
// to keep runtime reasonable for the demo.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // Population sizes (100x scaled down from Julia).
  let ne = 20
  let ni1 = 5
  let ni2 = 5

  // ----- Build populations -----
  // E = Tripod (homogeneous AdEx soma + 2 dendrites).
  let e_param = @snn_mbt.AdExParameter::new()
  let e_pop = @snn_mbt.Tripod::new(ne, e_param, rng)

  // I1 = IF with τm = 7ms, El = -55mV.
  let i1_param = @snn_mbt.IFParameter::custom(7.0F, -50.0F, -60.0F, -55.0F, 1.0F)
  let i1_pop = @snn_mbt.IF::new(ni1, i1_param, rng)

  // I2 = IF with τm = 20ms, El = -55mV.
  let i2_param = @snn_mbt.IFParameter::custom(20.0F, -50.0F, -60.0F, -55.0F, 1.0F)
  let i2_pop = @snn_mbt.IF::new(ni2, i2_param, rng)

  // ----- Build connections -----
  // E → I1, E → I2: would normally be SpikingSynapse(E, I, :ge) but
  // SpikingSynapse requires IF as pre. We approximate by adding a
  // constant tonic current to I1, I2 instead — this gives them a
  // baseline drive without requiring a Tripod-as-source path.
  // (A real port would build a TripodSource → IF wiring.)
  // For demo purposes, we add a tonic current to make I1, I2 active.
  for k in 0..<ni1 { i1_pop.i[k] = 250.0F }
  for k in 0..<ni2 { i2_pop.i[k] = 250.0F }

  // I1 → E on :soma with iSTDPRate.
  let i1_to_e = @snn_mbt.CompartmentSynapseTripod::random(
    i1_pop, e_pop, "ge", "soma", 5.0F, 0.0F, 0.2F, rng,
  )

  // I2 → E on :d1 with iSTDPPotential (here :d1, :hi in Julia).
  let i2_to_e = @snn_mbt.CompartmentSynapseTripod::random(
    i2_pop, e_pop, "ge", "d1", 5.0F, 0.0F, 0.2F, rng,
  )

  // ----- STDP entries -----
  // Note: CompartmentSynapseTripod uses IF as pre, so STDP can be
  // applied per-connection just like in `istdp_rate_step`.
  let i1_to_e_entry = @snn_mbt.IstdpRateEntry::new(0, ni1, ne)
  let i2_to_e_entry = @snn_mbt.IstdpPotentialEntry::new(0, ni2, ne)

  println("tripod_network.mbt: E-I Tripod network (manual stepping)")
  println("")
  println("=== Populations ===")
  println("  E   = Tripod x " + ne.to_string() + " (homogeneous AdEx soma)")
  println("  I1  = IF x " + ni1.to_string() + " (τm = 7ms, El = -55mV)")
  println("  I2  = IF x " + ni2.to_string() + " (τm = 20ms, El = -55mV)")
  println("")
  println("=== Connections ===")
  println("  I1 -> E (:soma)  μ=5.0, p=0.2  [iSTDPRate]")
  println("  I2 -> E (:d1)    μ=5.0, p=0.2  [iSTDPPotential]")
  println("  E  -> I1, E -> I2: approximated via tonic current (250pA)")
  println("  E  -> E recurrent: skipped (Tripod can't be pre in")
  println("                       CompartmentSynapseTripod)")
  println("")

  // ----- Manual sim loop -----
  let duration_ms = 500.0F
  let total_steps : Int = (duration_ms / dt).to_int()
  let mut e_spikes = 0
  let mut i1_spikes = 0
  let mut i2_spikes = 0
  for step in 0..<total_steps {
    let _ = step
    // 1. Forward compartment synapses (IF → Tripod).
    @snn_mbt.forward_compartment_tripod(i1_to_e, Float::from_int(step) * dt)
    @snn_mbt.forward_compartment_tripod(i2_to_e, Float::from_int(step) * dt)
    // 2. Step IF populations.
    @snn_mbt.step_synapses(i1_pop, dt)
    @snn_mbt.synaptic_current(i1_pop)
    @snn_mbt.step_neuron(i1_pop, dt)
    @snn_mbt.step_synapses(i2_pop, dt)
    @snn_mbt.synaptic_current(i2_pop)
    @snn_mbt.step_neuron(i2_pop, dt)
    // 3. Step Tripod (E) population.
    @snn_mbt.step_tripod(e_pop, dt)
    // 4. Apply STDP! updates. Note: these go to compartment synapse
    //    weight matrices via rowptr/colptr indexing, which matches the
    //    IF→Tripod connection layout.
    @snn_mbt.istdp_rate_step(
      i1_to_e.matrix.vals,
      i1_pop.fire,
      e_pop.fire,
      i1_to_e.matrix.colptr,
      i1_to_e.matrix.rowptr,
      i1_to_e_entry.vars,
      i1_to_e_entry.param,
      Float::from_int(step) * dt,
      dt,
    )
    @snn_mbt.istdp_potential_step(
      i2_to_e.matrix.vals,
      i2_pop.fire,
      e_pop.fire,
      i2_to_e.matrix.colptr,
      i2_to_e.matrix.rowptr,
      e_pop.v_d1,
      i2_to_e_entry.vars,
      i2_to_e_entry.param,
      Float::from_int(step) * dt,
      dt,
    )
    // 5. Accumulate spike counts.
    for k in 0..<ne { if e_pop.fire[k] { e_spikes = e_spikes + 1 } }
    for k in 0..<ni1 { if i1_pop.fire[k] { i1_spikes = i1_spikes + 1 } }
    for k in 0..<ni2 { if i2_pop.fire[k] { i2_spikes = i2_spikes + 1 } }
  }

  println("=== Simulation done (" + duration_ms.to_string() + " ms, dt=0.125ms) ===")
  println("  E  soma spikes: " + e_spikes.to_string())
  println("  I1 spikes:      " + i1_spikes.to_string())
  println("  I2 spikes:      " + i2_spikes.to_string())
  let e_rate = Float::from_int(e_spikes) * 1000.0F / duration_ms / Float::from_int(ne)
  let i1_rate = Float::from_int(i1_spikes) * 1000.0F / duration_ms / Float::from_int(ni1)
  let i2_rate = Float::from_int(i2_spikes) * 1000.0F / duration_ms / Float::from_int(ni2)
  println("  E  mean firing rate: " + e_rate.to_string() + " Hz/neuron")
  println("  I1 mean firing rate: " + i1_rate.to_string() + " Hz/neuron")
  println("  I2 mean firing rate: " + i2_rate.to_string() + " Hz/neuron")
  println("")
  println("Note: the Julia version uses SynapseNormalization (MultiplicativeNorm)")
  println("  to keep recurrent weights in check. That infrastructure isn't ported yet,")
  println("  so this demo skips E→E recurrent connections entirely.")
}