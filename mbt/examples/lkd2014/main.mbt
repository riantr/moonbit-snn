// lkd2014.mbt — port of LitwinKumar2014_vSTDPNetwork.jl (simplified).
//
// Original Julia (LitwinKumar2014_vSTDPNetwork.jl):
//   LKD_AdEx_exc = AdExSinExpParameter(τm=20ms, τe=6ms, τi=2ms,
//                                       Vt=-52mV, Vr=-60mV, El=-70mV, R=R)
//   LKD_AdEx_inh = AdExSinExpParameter(τm=20ms, τe=6ms, τi=2ms,
//                                       Vt=-52mV, Vr=-60mV, El=-62mV, R=R)
//   N = 1000, νe=4.5Hz, νi=2.5Hz
//   μEE=2.76/C, μEI=1.27/C, μIE=48.7/C, μII=16.2/C   (where C=300pF)
//   E = AdEx(N=4000); I = AdEx(N=1000)
//   EE/EI/IE/II SpikingSynapses with p=0.2
//   Input_E = PoissonStimulus(E, :ge, νe)
//   Input_I = PoissonStimulus(I, :ge, νi)
//   sim!(duration = 15s)
//
// MoonBit (v0.8.9): scaled 100× down (40 E + 10 I), default IF
// instead of AdEx (we don't have AdExSinExpParameter yet), Poisson
// rates scaled down by 100×. Behaviour should match qualitatively:
// both populations fire under background drive; I cells fire
// faster (because they have larger synaptic weights from E).

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne = 40
  let ni = 10
  let dt = 0.125F

  // IF parameters (we use IF instead of AdEx; no AdExSinExpParameter).
  let e_param = @snn_mbt.IFParameter::new()
  let i_param = @snn_mbt.IFParameter::with_el(-62.0F)
  let e_pop = @snn_mbt.IF::new(ne, e_param, rng)
  let i_pop = @snn_mbt.IF::new(ni, i_param, rng)
  // Tonic current to overcome the El→Vt gap (~333 pA needed).
  for k in 0..<ne { e_pop.i[k] = 350.0F }
  for k in 0..<ni { i_pop.i[k] = 250.0F }

  // Random connectivity.
  let ee = @snn_mbt.SpikingSynapse::random(e_pop, e_pop, "ge",
    2.76F, 0.0F, 0.2F, rng)
  let ei = @snn_mbt.SpikingSynapse::random(e_pop, i_pop, "ge",
    1.27F, 0.0F, 0.2F, rng)
  let ie = @snn_mbt.SpikingSynapse::random(i_pop, e_pop, "gi",
    48.7F, 0.0F, 0.2F, rng)
  let ii = @snn_mbt.SpikingSynapse::random(i_pop, i_pop, "gi",
    16.2F, 0.0F, 0.2F, rng)

  // Poisson inputs (scaled down 100× from 4.5Hz/2.5Hz).
  // In MoonBit internal units: 4.5 Hz → 0.0045F.
  let stim_e = @snn_mbt.PoissonStimulusIF::new(e_pop, "ge", 0.0045F, rng)
  let stim_i = @snn_mbt.PoissonStimulusIF::new(i_pop, "ge", 0.0025F, rng)

  // Build model.
  let fm_e = @snn_mbt.Monitor::new_fire(e_pop, 0)
  let fm_i = @snn_mbt.Monitor::new_fire(i_pop, 0)
  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(e_pop), @snn_mbt.IF_(i_pop)],
    [ee, ei, ie, ii],
    stims=[@snn_mbt.PoissonIF_(stim_e), @snn_mbt.PoissonIF_(stim_i)],
    monitors=[fm_e, fm_i],
  )

  // Simulate 1s (15× scaled down from 15s).
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  for _ in 0..<steps {
    @snn_mbt.step_heterogeneous(model, dt)
  }

  let e_hz = fm_e.firing_rate()
  let i_hz = fm_i.firing_rate()
  let e_count = fm_e.count_spikes()
  let i_count = fm_i.count_spikes()
  println("lkd2014.mbt: simplified LitwinKumar2014 vSTDP network")
  println("  E: " + ne.to_string() + " IF neurons (El=-70)")
  println("  I: " + ni.to_string() + " IF neurons (El=-62)")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  EI connections: " + ei.matrix.nnz().to_string())
  println("  IE connections: " + ie.matrix.nnz().to_string())
  println("  II connections: " + ii.matrix.nnz().to_string())
  println("  Poisson: 4.5Hz on E, 2.5Hz on I (scaled down 100×)")
  println("lkd2014.mbt: simulation done (1s)")
  println("  E[0] spikes: " + e_count.to_string() + " (" + e_hz.to_string() + " Hz)")
  println("  I[0] spikes: " + i_count.to_string() + " (" + i_hz.to_string() + " Hz)")
}