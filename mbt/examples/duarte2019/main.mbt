// duarte2019.mbt — Duarte2019 E-I network, port of
// `refs/SNNUtils.jl/src/models/duarte2019.jl` (PV + SST interneurons
// with IFParameterGsyn conductance scaling).
//
// Julia original:
//   PV  = IFParameterGsyn(τm=104.52pF/9.75nS, El=-64.33mV, Vt=-38.97mV,
//                        Vr=-57.47mV, gsyn_e=1.04nS, gsyn_i=0.84nS)
//   SST = IFParameterGsyn(τm=102.86pF/4.61nS, El=-61mV, Vt=-34.4mV,
//                        Vr=-47.11mV, gsyn_e=0.56nS, gsyn_i=0.59nS)
//   + E population with IFParameterGsyn(gsyn_e=0.73nS, gsyn_i=0.265nS)
//   + PoissonStimulus drive to each population
//   + sim!(duration = …)
//
// MoonBit (v0.10.63):
//   - Uses IFParameterGsyn (Duarte2019 / LKD2014-style per-population
//     synaptic conductance scale).
//   - Scaled down 100× (20 E + 8 PV + 8 SST), 100ms sim duration.
//   - Output: per-population mean firing rate over the sim window.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 20
  let npv = 8
  let nsst = 8
  let dt = 0.125F

  // ----- E population (gsyn_e = 0.73nS, gsyn_i = 0.265nS) -----
  // Use IFParameter::with_el to set a more firing-friendly Vt
  // (vt=-55 instead of -50) so the tonic current can drive spiking.
  // With el=-70, vt=-55, r=0.06, the required current to fire is
  // (vt - el) / r = 250 pA. Tonic 400 pA → steady-state at -46mV,
  // above Vt → fires.
  let e_base = @snn_mbt.IFParameter::with_el(-70.0F)
  let e_pop = @snn_mbt.IF::with_gsyn(ne, e_base, 0.73F, 0.265F, rng)

  // ----- PV population (gsyn_e = 1.04nS, gsyn_i = 0.84nS) -----
  let pv_pop = @snn_mbt.IF::with_gsyn(npv, e_base, 1.04F, 0.84F, rng)

  // ----- SST population (gsyn_e = 0.56nS, gsyn_i = 0.59nS) -----
  let sst_pop = @snn_mbt.IF::with_gsyn(nsst, e_base, 0.56F, 0.59F, rng)

  // Tonic drive (above firing threshold: needs >250pA for Vt=-55, El=-70).
  for k in 0..<ne { e_pop.i[k] = 400.0F }
  for k in 0..<npv { pv_pop.i[k] = 350.0F }
  for k in 0..<nsst { sst_pop.i[k] = 300.0F }

  // ----- Connectivity (simplified) -----
  let e_to_pv = @snn_mbt.SpikingSynapse::random(
    e_pop, pv_pop, "ge", 1.0F, 0.0F, 0.3F, rng,
  )
  let e_to_sst = @snn_mbt.SpikingSynapse::random(
    e_pop, sst_pop, "ge", 1.0F, 0.0F, 0.3F, rng,
  )
  let pv_to_e = @snn_mbt.SpikingSynapse::random(
    pv_pop, e_pop, "gi", 2.0F, 0.0F, 0.3F, rng,
  )
  let sst_to_e = @snn_mbt.SpikingSynapse::random(
    sst_pop, e_pop, "gi", 2.0F, 0.0F, 0.3F, rng,
  )

  // ----- Monitors for spike counting -----
  let fm_e = @snn_mbt.Monitor::new_fire(e_pop, 0)
  let fm_pv = @snn_mbt.Monitor::new_fire(pv_pop, 0)
  let fm_sst = @snn_mbt.Monitor::new_fire(sst_pop, 0)

  // ----- Manual sim loop (100ms, dt=0.125ms → 800 steps) -----
  let duration_ms = 100.0F
  let steps : Int = (duration_ms / dt).to_int()
  for step in 0..<steps {
    let t_now = Float::from_int(step) * dt

    // Forward pre-spikes through synapses.
    @snn_mbt.forward_synapse(e_to_pv, t_now)
    @snn_mbt.forward_synapse(e_to_sst, t_now)
    @snn_mbt.forward_synapse(pv_to_e, t_now)
    @snn_mbt.forward_synapse(sst_to_e, t_now)

    // Integrate.
    @snn_mbt.step_synapses(e_pop, dt)
    @snn_mbt.synaptic_current(e_pop)
    @snn_mbt.step_neuron(e_pop, dt)
    @snn_mbt.step_synapses(pv_pop, dt)
    @snn_mbt.synaptic_current(pv_pop)
    @snn_mbt.step_neuron(pv_pop, dt)
    @snn_mbt.step_synapses(sst_pop, dt)
    @snn_mbt.synaptic_current(sst_pop)
    @snn_mbt.step_neuron(sst_pop, dt)

    // Record.
    @snn_mbt.record_one(fm_e, t_now)
    @snn_mbt.record_one(fm_pv, t_now)
    @snn_mbt.record_one(fm_sst, t_now)
  }

  // ----- Print per-population firing rates -----
  let t_sim_ms = duration_ms
  let e_total = @snn_mbt.Monitor::count_spikes(fm_e)
  let pv_total = @snn_mbt.Monitor::count_spikes(fm_pv)
  let sst_total = @snn_mbt.Monitor::count_spikes(fm_sst)
  let e_rate = Float::from_int(e_total) / Float::from_int(ne) / (t_sim_ms / 1000.0F)
  let pv_rate = Float::from_int(pv_total) / Float::from_int(npv) / (t_sim_ms / 1000.0F)
  let sst_rate = Float::from_int(sst_total) / Float::from_int(nsst) / (t_sim_ms / 1000.0F)
  println(
    "duarte2019.mbt: E-I network with PV + SST interneurons (IFParameterGsyn)",
  )
  println("")
  println("=== Populations ===")
  println("  E   = IF x " + ne.to_string() + " (gsyn_e=0.73, gsyn_i=0.265)")
  println("  PV  = IF x " + npv.to_string() + " (gsyn_e=1.04, gsyn_i=0.84)")
  println("  SST = IF x " + nsst.to_string() + " (gsyn_e=0.56, gsyn_i=0.59)")
  println("")
  println("=== Firing rates over " + t_sim_ms.to_string() + " ms ===")
  println("  E   = " + e_rate.to_string() + " Hz")
  println("  PV  = " + pv_rate.to_string() + " Hz")
  println("  SST = " + sst_rate.to_string() + " Hz")
}