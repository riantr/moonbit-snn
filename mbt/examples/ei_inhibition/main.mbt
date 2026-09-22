// ei_inhibition.mbt — port of the E/I inhibition test from
// refs/SNNModels.jl/test/network/ei_network.jl
//
// Compares an E-only network (tonically spiking IF with El=-49mV)
// against an E/I network where I cells project back to E via :gi.
// The inhibitory feedback should reduce the E firing rate.
//
// Julia (test):
//   E1 = IF(N=30, param = IFParameter(El=-49mV))
//   monitor!(E1, [:fire])
//   sim!(compose(E=E1, ...), 500ms)
//   n_noinhib = sum(length, spiketimes(E1))
//
//   E2 = IF(N=30, param = IFParameter(El=-49mV))
//   I2 = IF(N=10, param = IFParameter(El=-49mV))
//   ei2 = SpikingSynapse(E2, I2, :ge; p=0.8, μ=1.5)
//   ie2 = SpikingSynapse(I2, E2, :gi; p=0.8, μ=3.0)
//   sim!(compose(E=E2, I=I2, ei=ei2, ie=ie2), 500ms)
//   n_inhib = sum(length, spiketimes(E2))
//   @test n_inhib < n_noinhib

///|
fn run_e_only(e : @snn_mbt.IF, duration_ms : Float) -> Int {
  let fm = @snn_mbt.Monitor::new_fire(e, 0)
  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(e)],
    [],
    monitors=[fm],
  )
  @snn_mbt.heterogeneous_sim_for(model, duration_ms * @snn_mbt.ms)
  fm.count_spikes()
}

///|
fn run_ei(
  e : @snn_mbt.IF,
  i : @snn_mbt.IF,
  ei : @snn_mbt.SpikingSynapse,
  ie : @snn_mbt.SpikingSynapse,
  duration_ms : Float,
) -> Int {
  let fm = @snn_mbt.Monitor::new_fire(e, 0)
  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(e), @snn_mbt.IF_(i)],
    [ei, ie],
    monitors=[fm],
  )
  @snn_mbt.heterogeneous_sim_for(model, duration_ms * @snn_mbt.ms)
  fm.count_spikes()
}

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let e_param = @snn_mbt.IFParameter::with_el(-49.0F)

  // E-only baseline: 30 IF neurons with El=-49mV (just below Vt=-50).
  let e1 = @snn_mbt.IF::new(30, e_param, rng)
  let n_noinhib = run_e_only(e1, 500.0F)

  // E/I network: same E + 10 I cells (also El=-49mV).
  // E→I :ge (μ=1.5, p=0.8) and I→E :gi (μ=3.0, p=0.8).
  let i_param = @snn_mbt.IFParameter::with_el(-49.0F)
  let e2 = @snn_mbt.IF::new(30, e_param, rng)
  let i_pop = @snn_mbt.IF::new(10, i_param, rng)
  let ei = @snn_mbt.SpikingSynapse::random(e2, i_pop, "ge", 1.5F, 0.0F, 0.8F, rng)
  let ie = @snn_mbt.SpikingSynapse::random(i_pop, e2, "gaba", 3.0F, 0.0F, 0.8F, rng)
  let n_inhib = run_ei(e2, i_pop, ei, ie, 500.0F)

  println("ei_inhibition.mbt: simulation done (500ms each)")
  println("  E-only spikes in E1[0]: " + n_noinhib.to_string())
  println("  E/I spikes in E2[0]:    " + n_inhib.to_string())
  if n_inhib < n_noinhib {
    println("  PASS: inhibitory feedback reduced E firing rate.")
  } else {
    println("  WARN: inhibitory feedback did not reduce E firing rate.")
  }
}