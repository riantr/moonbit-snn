// IF (Integrate-and-Fire) neuron — bit-exact port of SNNModels.jl.
//
// Julia reference:
//   src/populations/generized_if/if.jl
//
// MoonBit identifier note: Julia uses Greek letters (τ, Δ) for the
// time-constant / slope-factor field names; MoonBit's lexer does not
// accept non-ASCII identifier characters. We use ASCII names
// (`tm`, `tw`, `dt_slope`, `tabs_const`, `tre`, `tde`, `tri`, `tdi`).
//
// Float32 contract: every arithmetic operation uses `Float` (Float32).
// Update order matches the Julia source exactly:
//
//   v[i] += dt/tm * (-(v[i] - El) + R*(-w[i] + I[i]) - R*syn_curr[i])
//   fire[i] = v[i] > Vt
//   v[i] = ifelse(fire[i], Vr, v[i])
//   tabs[i] = ifelse(fire[i], round(Int, τabs / dt), tabs[i])

///|
/// IFParameter — holds the biophysical constants of an LIF neuron.
///
/// Field values match the Julia defaults in Float32 arithmetic.
pub struct IFParameter {
  c : Float
  gl : Float
  tm : Float
  vt : Float
  vr : Float
  el : Float
  r : Float
  dt_slope : Float
  a : Float
  b : Float
  tw : Float
}

///|
/// Default IFParameter, matching Julia's `IFParameter()`.
pub fn IFParameter::new() -> IFParameter {
  let c : Float = -1.0F
  let gl : Float = -1.0F
  { c, gl, tm: 15.0F, vt: -50.0F, vr: -60.0F, el: -70.0F, r: 0.06F,
    dt_slope: 2.0F, a: 0.0F, b: 0.0F, tw: 0.0F }
}

///|
/// IFParameter with a custom resting potential `el`. Matches
/// `SNN.IFParameter(; El = -49mV)` from IF_net.jl.
pub fn IFParameter::with_el(el : Float) -> IFParameter {
  let c : Float = -1.0F
  let gl : Float = -1.0F
  { c, gl, tm: 15.0F, vt: -50.0F, vr: -60.0F, el, r: 0.06F,
    dt_slope: 2.0F, a: 0.0F, b: 0.0F, tw: 0.0F }
}

///|
/// IFParameter with custom time-constant, threshold, reset, resting
/// potential, and resistance. Matches
/// `SNN.IFParameter(; τm=20ms, R=100MΩ, Vt=-50mV, Vr=-60mV, El=-60mV)`
/// from CUBA.jl. Note: in MoonBit normalised units, `R` is in mho;
/// `100*MΩ = 100*mohm = 0.1F`.
pub fn IFParameter::custom(tm : Float, vt : Float, vr : Float, el : Float, r : Float) -> IFParameter {
  let c : Float = -1.0F
  let gl : Float = -1.0F
  { c, gl, tm, vt, vr, el, r, dt_slope: 2.0F, a: 0.0F, b: 0.0F, tw: 0.0F }
}

///|
/// PostSpike parameters — absolute refractory period.
pub struct PostSpike {
  tabs_const : Float
}

///|
pub fn PostSpike::new() -> PostSpike {
  // Julia: PostSpike{Float32}(; τabs = 2ms)
  { tabs_const: 2.0F }
}

///|
/// IF neuron state — a population of N integrate-and-fire neurons.
pub struct IF {
  param : IFParameter
  spike : PostSpike
  n : Int
  v : Array[Float]
  w : Array[Float]
  fire : Array[Bool]
  tabs : Array[Int]
  i : Array[Float]
  syn_curr : Array[Float]
  // Synapse state
  ge : Array[Float]
  gi : Array[Float]
  he : Array[Float]
  hi : Array[Float]
  glu : Array[Float]
  gaba : Array[Float]
  gsyn_e : Array[Float]
  gsyn_i : Array[Float]
  // Receptor reversal
  e_e : Float
  e_i : Float
  // Synapse time constants (DoubleExpSynapse defaults)
  tre : Float
  tde : Float
  tri : Float
  tdi : Float
}

///|
/// Construct a new IF population with `n` neurons.
pub fn IF::new(n : Int, param : IFParameter, rng : Xoshiro) -> IF {
  let v = Array::make(n, 0.0F)
  let spread = param.vt - param.vr
  for k in 0..<n {
    v[k] = param.vr + next_f32(rng) * spread
  }
  let w : Array[Float] = Array::make(n, 0.0F)
  let fire : Array[Bool] = Array::make(n, false)
  let tabs : Array[Int] = Array::make(n, 0)
  let i : Array[Float] = Array::make(n, 0.0F)
  let syn_curr : Array[Float] = Array::make(n, 0.0F)
  let ge : Array[Float] = Array::make(n, 0.0F)
  let gi : Array[Float] = Array::make(n, 0.0F)
  let he : Array[Float] = Array::make(n, 0.0F)
  let hi : Array[Float] = Array::make(n, 0.0F)
  let glu : Array[Float] = Array::make(n, 0.0F)
  let gaba : Array[Float] = Array::make(n, 0.0F)
  let gsyn_e : Array[Float] = Array::make(n, 1.0F)
  let gsyn_i : Array[Float] = Array::make(n, 1.0F)
  let e_e : Float = 0.0F
  let e_i : Float = -75.0F
  let tre : Float = 1.0F
  let tde : Float = 6.0F
  let tri : Float = 0.5F
  let tdi : Float = 2.0F
  { param, spike: PostSpike::new(), n, v, w, fire, tabs, i, syn_curr,
    ge, gi, he, hi, glu, gaba, gsyn_e, gsyn_i,
    e_e, e_i, tre, tde, tri, tdi }
}

///|
/// Update the synaptic state variables (he, hi, ge, gi) for one step.
/// Bit-exact port of DoubleExpSynapse.update_synapses!.
pub fn step_synapses(p : IF, dt : Float) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.he[i] = p.he[i] + p.glu[i]
    p.hi[i] = p.hi[i] + p.gaba[i]
    p.ge[i] = p.ge[i] + dt * (-p.ge[i] / p.tde + p.he[i])
    p.he[i] = p.he[i] + dt * (-p.he[i] / p.tre)
    p.gi[i] = p.gi[i] + dt * (-p.gi[i] / p.tdi + p.hi[i])
    p.hi[i] = p.hi[i] + dt * (-p.hi[i] / p.tri)
  }
  for i in 0..<n {
    p.glu[i] = 0.0F
    p.gaba[i] = 0.0F
  }
  ()
}

///|
/// Compute synaptic current into each neuron.
pub fn synaptic_current(p : IF) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.syn_curr[i] = p.ge[i] * (p.v[i] - p.e_e) * p.gsyn_e[i] +
                    p.gi[i] * (p.v[i] - p.e_i) * p.gsyn_i[i]
  }
  ()
}

///|
/// Update the IF membrane potential for one step.
pub fn step_neuron(p : IF, dt : Float) -> Unit {
  let n = p.n
  let p_ = p.param
  let tm = p_.tm
  let el = p_.el
  let r = p_.r
  let vt = p_.vt
  let vr = p_.vr
  let tabs_const = p.spike.tabs_const
  let tabs_steps : Int = (tabs_const / dt).to_int()

  for i in 0..<n {
    if p.tabs[i] > 0 {
      p.fire[i] = false
      p.tabs[i] = p.tabs[i] - 1
      continue
    }
    p.v[i] = p.v[i] + dt / tm *
      (-(p.v[i] - el) + r * (-p.w[i] + p.i[i]) - r * p.syn_curr[i])
    p.fire[i] = p.v[i] > vt
    p.v[i] = if p.fire[i] { vr } else { p.v[i] }
    p.tabs[i] = if p.fire[i] { tabs_steps } else { p.tabs[i] }
  }
  ()
}
