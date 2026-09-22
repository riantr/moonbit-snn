// lkd2014_adex.mbt — Litwin-Kumar-Doiron 2014 vSTDP network, port
// with proper AdExSinExpParameter (vs lkd2014.mbt which uses IF).
//
// Julia reference (SNNUtils.jl/src/models/lkd2014.jl, LKD2014SingleExp):
//   AdEx = AdExSinExpParameter(El=-70mV, Vt=-52mV, Vr=-60mV,
//                              τm=20ms, R=1/15nS, At=10mV, τe=6ms, τi=2ms)
//   N = 1000, νe=4.5Hz, νi=2.5Hz
//   μEE=2.76/C, μEI=1.27/C, μIE=48.7/C, μII=16.2/C   (C=300pF)
//   sim!(duration = 15s)
//
// MoonBit (v0.10.10):
//   - E/I populations use AdExSinExp with LKD defaults.
//   - Pre-spike routing is done manually (SpikingSynapse is hardcoded
//     to IF pre / IF post, so we route IF pre-spikes into the post's
//     glu/gaba buffer directly).
//   - Scaled down 100× (40 E + 10 I), 1s sim duration.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 40
  let ni = 10
  let dt = 0.125F

  // E population: AdExSinExp with LKD defaults + tonic drive 350 pA.
  let e_param = @snn_mbt.AdExSinExpParameter::lkd_adex()
  let e_pop = @snn_mbt.AdExSinExp::new(ne, e_param, rng)
  for k in 0..<ne { e_pop.i[k] = 350.0F }
  // I population: same AdExSinExp but with El=-62mV (PV-like).
  let i_param : @snn_mbt.AdExSinExpParameter = { ..@snn_mbt.AdExSinExpParameter::lkd_adex(), el: -62.0F }
  let i_pop = @snn_mbt.AdExSinExp::new(ni, i_param, rng)
  for k in 0..<ni { i_pop.i[k] = 250.0F }

  // Pre-population: IF (used as input spike source for the manual loop).
  let e_pre = @snn_mbt.IF::new(ne, @snn_mbt.IFParameter::new(), rng)
  let i_pre = @snn_mbt.IF::new(ni, @snn_mbt.IFParameter::new(), rng)

  // Connectivity (random Bernoulli, p=0.2; weights as in LKD).
  // EE connections: E_pre → E (μ=2.76 nS).
  let ee_w = @snn_mbt.make_random_weights(ne, ne, 2.76F, 0.2F, rng)
  // EI connections: I_pre → E (μ=1.27 nS).
  let ei_w = @snn_mbt.make_random_weights(ni, ne, 1.27F, 0.2F, rng)
  // IE connections: E_pre → I (μ=48.7 nS).
  let ie_w = @snn_mbt.make_random_weights(ne, ni, 48.7F, 0.2F, rng)
  // II connections: I_pre → I (μ=16.2 nS).
  let ii_w = @snn_mbt.make_random_weights(ni, ni, 16.2F, 0.2F, rng)

  // Poisson inputs (scaled down 100× from 4.5Hz/2.5Hz).
  let e_stim = @snn_mbt.PoissonStimulusIF::new(e_pre, "ge", 0.0045F, rng)
  let i_stim = @snn_mbt.PoissonStimulusIF::new(i_pre, "ge", 0.0025F, rng)

  // Monitors for spike counting.
  let fm_e = @snn_mbt.MonitorAdExSinExp::new_fire(e_pop, 0)
  let fm_i = @snn_mbt.MonitorAdExSinExp::new_fire(i_pop, 0)

  // Manual sim loop (1s, dt=0.125ms → 8000 steps).
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  for step in 0..<steps {
    let t_now = Float::from_int(step) * dt
    // 1. Drive pre-neurons (IF) with Poisson + decay their synapses.
    @snn_mbt.stimulate_if(e_stim, t_now, dt)
    @snn_mbt.stimulate_if(i_stim, t_now, dt)
    @snn_mbt.simulate_step_if(e_pre, dt)
    @snn_mbt.simulate_step_if(i_pre, dt)
    // 2. Route pre-spikes to post-synaptic glu/gaba buffers.
    //    EE + IE → post.glu (excitatory).
    //    EI + II → post.gaba (inhibitory).
    @snn_mbt.route_pre_to_post(e_pre, e_pop, ee_w, true, false)
    @snn_mbt.route_pre_to_post(i_pre, e_pop, ei_w, false, true)
    @snn_mbt.route_pre_to_post(e_pre, i_pop, ie_w, true, false)
    @snn_mbt.route_pre_to_post(i_pre, i_pop, ii_w, false, true)
    // 3. Integrate AdExSinExp posts.
    @snn_mbt.adex_sinexp_step_synapses(e_pop, dt)
    @snn_mbt.adex_sinexp_synaptic_current(e_pop)
    @snn_mbt.step_adex_sinexp(e_pop, dt)
    @snn_mbt.adex_sinexp_step_synapses(i_pop, dt)
    @snn_mbt.adex_sinexp_synaptic_current(i_pop)
    @snn_mbt.step_adex_sinexp(i_pop, dt)
    // 4. Record monitor.
    @snn_mbt.record_one_sinexp(fm_e, t_now)
    @snn_mbt.record_one_sinexp(fm_i, t_now)
  }

  let e_hz = @snn_mbt.monitor_firing_rate(fm_e.data)
  let i_hz = @snn_mbt.monitor_firing_rate(fm_i.data)
  let e_count = @snn_mbt.monitor_count_spikes(fm_e.data)
  let i_count = @snn_mbt.monitor_count_spikes(fm_i.data)
  let ee_count = @snn_mbt.count_nnz(ee_w)
  let ei_count = @snn_mbt.count_nnz(ei_w)
  let ie_count = @snn_mbt.count_nnz(ie_w)
  let ii_count = @snn_mbt.count_nnz(ii_w)
  println("lkd2014_adex.mbt: LKD 2014 vSTDP network with AdExSinExp")
  println("  E: " + ne.to_string() + " AdExSinExp neurons (El=-70, Vt=-52, Vr=-60)")
  println("  I: " + ni.to_string() + " AdExSinExp neurons (El=-62, Vt=-52, Vr=-60)")
  println("  EE: " + ee_count.to_string() + " conns (μ=2.76 nS)")
  println("  EI: " + ei_count.to_string() + " conns (μ=1.27 nS)")
  println("  IE: " + ie_count.to_string() + " conns (μ=48.7 nS)")
  println("  II: " + ii_count.to_string() + " conns (μ=16.2 nS)")
  println("  Poisson: 4.5Hz on E, 2.5Hz on I (scaled down 100×)")
  println("lkd2014_adex.mbt: simulation done (1s)")
  println("  E[0] spikes: " + e_count.to_string() + " (" + e_hz.to_string() + " Hz)")
  println("  I[0] spikes: " + i_count.to_string() + " (" + i_hz.to_string() + " Hz)")
}
