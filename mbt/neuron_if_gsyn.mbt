// neuron_if_gsyn.mbt — IF with population-wide synaptic conductance
// scaling (the `IFParameterGsyn` variant from Julia's
// `SNNModels.jl/src/populations/generized_if/if.jl`).
//
// Julia's `IFParameterGsyn` extends `IFParameter` with two scalar
// fields `gsyn_e` and `gsyn_i` (per-population synaptic conductance
// scale, in Siemens). In the Julia step function:
//
//   I_syn[i] = -ge[i] * (v[i] - E_e) * gsyn_e
//            - gi[i] * (v[i] - E_i) * gsyn_i
//
// In MoonBit, our `IF` struct already has per-neuron `gsyn_e[i]` and
// `gsyn_i[i]` arrays (default 1.0). `IFParameterGsyn` here is a
// parameter container that holds the scalar values and provides a
// method to apply them as a uniform per-neuron scale.
//
// Reference: `refs/SNNModels.jl/src/populations/generized_if/if.jl`
// and `refs/SNNUtils.jl/src/models/duarte2019.jl`.

///|
/// IFParameterGsyn — `IFParameter` + population-wide synaptic
/// conductance scales `gsyn_e` (excitatory) and `gsyn_i` (inhibitory).
/// All other IF dynamics are unchanged.
pub(all) struct IFParameterGsyn {
  // Underlying IF dynamics parameters (El, Vt, Vr, etc.).
  base : IFParameter
  // Population-wide excitatory conductance scale (Siemens).
  gsyn_e : Float
  // Population-wide inhibitory conductance scale (Siemens).
  gsyn_i : Float
}

///|
/// Construct an IFParameterGsyn from explicit field values.
/// Mirrors Julia's `IFParameterGsyn(τm = 104.52pF/9.75nS,
/// El = -64.33mV, Vt = -38.97mV, Vr = -57.47mV, τabs = 0.5ms,
/// gsyn_e = 1.04nS, gsyn_i = 0.84nS)`.
pub fn IFParameterGsyn::new(
  base : IFParameter,
  gsyn_e : Float,
  gsyn_i : Float,
) -> IFParameterGsyn {
  { base, gsyn_e, gsyn_i }
}

///|
/// Construct an IFParameterGsyn from an existing IFParameter, copying
/// the underlying dynamics and overriding only the gsyn scales. Used
/// to upgrade an `IFParameter` to a Gsyn variant without rebuilding
/// all the field-by-field.
pub fn IFParameterGsyn::from_base(
  base : IFParameter,
  gsyn_e : Float,
  gsyn_i : Float,
) -> IFParameterGsyn {
  { base, gsyn_e, gsyn_i }
}

///|
/// Apply the population-wide `gsyn_e` / `gsyn_i` scales uniformly to
/// every neuron's per-neuron gsyn arrays. Mirrors Julia's effect of
/// constructing an `IF` with an `IFParameterGsyn` (every neuron gets
/// the same conductance scale).
///
/// Usage:
///   let pop = IF::new(n, IFParameter::new(), rng)
///   let gs = IFParameterGsyn::from_base(IFParameter::new(), 1.04F, 0.84F)
///   gs.apply(pop)
pub fn IFParameterGsyn::apply(s : IFParameterGsyn, p : IF) -> Unit {
  let n = p.n
  let mut i = 0
  while i < n {
    p.gsyn_e[i] = s.gsyn_e
    p.gsyn_i[i] = s.gsyn_i
    i = i + 1
  }
}

///|
/// Convenience constructor: build an IF + apply gsyn scales in one step.
pub fn IF::with_gsyn(
  n : Int,
  base : IFParameter,
  gsyn_e : Float,
  gsyn_i : Float,
  rng : Xoshiro,
) -> IF {
  let p = IF::new(n, base, rng)
  let gs : IFParameterGsyn = { base, gsyn_e, gsyn_i }
  gs.apply(p)
  p
}