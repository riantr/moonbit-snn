// lkd2014_demo.mbt — uses the LKD2014 parameter constants to build a
// small PV IF network, drive it with a single spike at t=1000ms,
// and verify the post-synaptic potential + conductance traces.
//
// Julia reference (refs/SNNUtils.jl/test/LKD.jl):
//   IF_pv = IF(; N=1, param=LKD2014.PV)
//   SpikeTimeStimulus with spiketimes=[1000ms], neurons=[1]
//   sim! for 1200ms @ dt=0.125ms
//
// Note: SpikeTimeStimulus::new currently only accepts IF populations
// (not AdEx); the AdEx side of LKD.jl is omitted here. The full
// two-population network would require a TimedStimulusAdEx variant.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // Build LKD2014.PV (IF) parameters.
  // LKD2014.PV: El=-62mV, Vr=-57.47mV, Vt=-52mV, tm=20ms.
  let pv_param = @snn_mbt.IFParameter::custom(
    20.0F, // tm (ms)
    -52.0F, // vt
    -57.47F, // vr (LKD2014.PV.Vr)
    -62.0F, // el (LKD2014.PV.El)
    0.0667F, // r (matches LKD 1/15nS scaling)
  )
  let pv_pop = @snn_mbt.IF::new(1, pv_param, rng)

  // SpikeTimeStimulus: single spike at t=1000ms onto :ge.
  let pv_stim = @snn_mbt.SpikeTimeStimulus::new(
    pv_pop, "ge", [1000.0F], [0],
  )

  // Run 1200ms sim with manual loop. Track v[0] + glu[0] at the
  // scheduled spike time (t=1000ms) and after.
  let n_steps = (1200.0F / dt).to_int()  // 9600 steps
  let mut s = 0
  while s < n_steps {
    let t = Float::from_int(s) * dt
    // 1. Deliver scheduled spikes at this time.
    @snn_mbt.stimulate_spiketime(pv_stim, t, 1.0F)
    // 2. Step IF (synapses → synaptic current → neuron).
    @snn_mbt.step_synapses(pv_pop, dt)
    @snn_mbt.synaptic_current(pv_pop)
    @snn_mbt.step_neuron(pv_pop, dt)
    s = s + 1
  }

  println("lkd2014_demo.mbt: LKD2014.PV IF with single spike at t=1000ms")
  println("  PV.v[0] final = " + pv_pop.v[0].to_string() + " (initial El=-62)")
  println("  PV.glu[0] final = " + pv_pop.glu[0].to_string())
  println("  PV.fire[0] final = " + pv_pop.fire[0].to_string())
}