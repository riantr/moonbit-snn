// Population dispatcher — type-driven integrate_step.
//
// Rather than a trait (which MoonBit's `impl Trait for Type` syntax
// doesn't fully support for cross-struct dispatch from another
// module), we use a sum type `AnyPop` to wrap any population and
// dispatch via pattern matching. This matches the architectural
// pattern in `SpikingNeuralNetworks.jl` where `AbstractPopulation`
// is a Julia abstract type with a union of concrete types.
//
// This is the `v0.0.8` workaround for the trait limitation.

///|
/// Heterogeneous population wrapper.
pub(all) enum AnyPop {
  IF_(@neuron.IF)
  AdEx_(@neuron.AdEx)
  AdExSinExp_(@neuron.AdExSinExp)
  IZ_(@neuron.IZ)
  HH_(@neuron.HH)
  ML_(@neuron.MorrisLecar)
  Poisson_(@neuron.Poisson)
  WC_(@neuron.WilsonCowan)
  HetRec_(@neuron.HetRec)
  IFCANAHP_(@neuron.IFCANAHP)
}

///|
// Dispatch a single integration step based on the enum variant.

///|
pub fn integrate_any(p : AnyPop, dt : Float) -> Unit {
  match p {
    IF_(x) => {
      @neuron.step_synapses(x, dt)
      @neuron.synaptic_current(x)
      @neuron.step_neuron(x, dt)
    }
    AdEx_(x) => {
      @neuron.adex_step_synapses(x, dt)
      @neuron.adex_synaptic_current(x)
      @neuron.step_adex(x, dt)
    }
    AdExSinExp_(x) => {
      @neuron.adex_sinexp_step_synapses(x, dt)
      @neuron.adex_sinexp_synaptic_current(x)
      @neuron.step_adex_sinexp(x, dt)
    }
    IZ_(x) => @neuron.step_iz(x, dt)
    HH_(x) => @neuron.step_hh(x, dt)
    ML_(x) => @neuron.step_ml(x, dt)
    Poisson_(x) => @neuron.step_poisson(x, dt)
    WC_(x) => @neuron.step_wc(x, dt)
    HetRec_(x) => @neuron.step_hetrec(x, dt)
    IFCANAHP_(x) => @neuron.integrate_ifcanahp(x, x.param, dt)
  }
}

///|
// Dispatch n_neurons based on the enum variant.

///|
pub fn any_n_neurons(p : AnyPop) -> Int {
  match p {
    IF_(x) => x.n
    AdEx_(x) => x.n
    AdExSinExp_(x) => x.n
    IZ_(x) => x.n
    HH_(x) => x.n
    ML_(x) => x.n
    Poisson_(x) => x.n
    WC_(x) => x.n
    HetRec_(x) => x.n
    IFCANAHP_(x) => x.n
  }
}

///|
// Heterogeneous sim loop. Matches SNN's `sim!([E1, E2], ...)`.

///|
pub fn sim_any_pops(pops : Array[AnyPop], dt : Float) -> Unit {
  for p in pops {
    integrate_any(p, dt)
  }
}

///|
// `n_total(pops)` — total neuron count across heterogeneous populations.

///|
pub fn n_total(pops : Array[AnyPop]) -> Int {
  let mut total = 0
  for p in pops {
    total = total + any_n_neurons(p)
  }
  total
}

///|
// `sim_any_pops_for(pops, duration, dt)` — full simulation in one call.

///|
pub fn sim_any_pops_for(pops : Array[AnyPop], duration : Float, dt : Float) -> Unit {
  let steps : Int = (duration / dt).to_int()
  for _ in 0..<steps {
    sim_any_pops(pops, dt)
  }
}
