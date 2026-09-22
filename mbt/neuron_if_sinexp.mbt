// neuron_if_sinexp.mbt — IF + single-exp synapse (LKD2014SingleExp.PV).
//
// Julia reference:
//   - LKD2014SingleExp.PV in SNNUtils.jl/src/models/lkd2014.jl
//     uses an IF neuron with τe=6ms, τi=2ms single-exp synapses.
//   - See AdExSinExp (neuron_adex_sinexp.mbt) for the single-exp
//     synapse update rule.
//
// Float32 contract: same as IF (every arithmetic uses `Float`).

///|
/// IFSinExpParameter — IF neuron parameters with single-exp synapse
/// time constants (τe, τi). Structurally identical to IFParameter;
/// kept as a separate type for documentation and to match Julia's
/// `IFSinExpParameter` named tuple in LKD2014SingleExp.
pub struct IFSinExpParameter {
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
  // Single-exp synapse time constants (LKD2014SingleExp.PV defaults).
  tau_e : Float
  tau_i : Float
}

///|
/// Default IFSinExpParameter, matching Julia's `IFSinExpParameter()`
/// with τe=6, τi=2.
pub fn IFSinExpParameter::new() -> IFSinExpParameter {
  let c : Float = -1.0F
  let gl : Float = -1.0F
  { c, gl, tm: 20.0F, vt: -50.0F, vr: -60.0F, el: -62.0F, r: 0.05F,
    dt_slope: 2.0F, a: 0.0F, b: 0.0F, tw: 0.0F,
    tau_e: 6.0F, tau_i: 2.0F }
}

///|
/// IFSinExpParameter with LKD 2014 PV defaults (El=-62mV, Vr=-57.47mV,
/// Vt=-52mV, τm=20ms, τe=6, τi=2). Matches Julia's
/// `IFSinExpParameter(El=-62, Vr=-57.47, Vt=-52, τm=20, τi=2, τe=6)`.
pub fn IFSinExpParameter::lkd_pv() -> IFSinExpParameter {
  let c : Float = -1.0F
  let gl : Float = -1.0F
  { c, gl, tm: 20.0F, vt: -52.0F, vr: -57.47F, el: -62.0F, r: 0.05F,
    dt_slope: 2.0F, a: 0.0F, b: 0.0F, tw: 0.0F,
    tau_e: 6.0F, tau_i: 2.0F }
}

///|
/// IFSinExp neuron state — population of N integrate-and-fire neurons
/// with single-exp synaptic dynamics.
pub struct IFSinExp {
  param : IFSinExpParameter
  spike : PostSpike
  n : Int
  v : Array[Float]
  w : Array[Float]
  fire : Array[Bool]
  tabs : Array[Int]
  i : Array[Float]
  syn_curr : Array[Float]
  // Synapse state (single-exp — no separate rise conductances)
  ge : Array[Float]
  gi : Array[Float]
  glu : Array[Float]
  gaba : Array[Float]
  gsyn_e : Array[Float]
  gsyn_i : Array[Float]
  // Records
  fire_t : Array[Int]
  fire_t_delta : Array[Int]
}

///|
/// Build an IFSinExp population of size n with `param` and random
/// initial state drawn from `rng`.
pub fn IFSinExp::new(
  n : Int,
  param : IFSinExpParameter,
  rng : Xoshiro,
) -> IFSinExp {
  let v : Array[Float] = Array::make(n, param.el)
  let w : Array[Float] = Array::make(n, 0.0F)
  let fire : Array[Bool] = Array::make(n, false)
  let tabs : Array[Int] = Array::make(n, 0)
  let i : Array[Float] = Array::make(n, 0.0F)
  let syn_curr : Array[Float] = Array::make(n, 0.0F)
  let ge : Array[Float] = Array::make(n, 0.0F)
  let gi : Array[Float] = Array::make(n, 0.0F)
  let glu : Array[Float] = Array::make(n, 0.0F)
  let gaba : Array[Float] = Array::make(n, 0.0F)
  let gsyn_e : Array[Float] = Array::make(n, 0.0F)
  let gsyn_i : Array[Float] = Array::make(n, 0.0F)
  let fire_t : Array[Int] = []
  let fire_t_delta : Array[Int] = Array::make(n, 0)
  // Random initial voltages via Box-Muller (small noise).
  for k in 0..<n {
    let (z1, _) = box_muller(rng)
    v[k] = param.el + Float::from_double(z1) * 2.0F
  }
  let _ = i
  { param, spike: PostSpike::new(), n, v, w, fire, tabs, i, syn_curr,
    ge, gi, glu, gaba, gsyn_e, gsyn_i, fire_t, fire_t_delta }
}

///|
/// Advance an IFSinExp population by one timestep of length `dt`.
/// 1. Membrane update: `dv = dt * ((v - el - r*i) / tm)` → `v += dv`.
/// 2. Single-exp synapse update:
///    `ge[i] += glu[i]; gi[i] += gaba[i];`
///    `ge[i] += dt * (-ge[i] / tau_e); gi[i] += dt * (-gi[i] / tau_i);`
///    `glu[i] = 0; gaba[i] = 0;`
/// 3. Fire check: if v >= vt, fire, reset v=vr, record fire_t.
pub fn ifsinexp_step(
  pop : IFSinExp,
  dt : Float,
) -> Unit {
  let n = pop.n
  let p = pop.param
  let mut k = 0
  while k < n {
    pop.fire[k] = false
    // Skip refractory
    if pop.tabs[k] > 0 {
      pop.tabs[k] = pop.tabs[k] - 1
    } else {
      // Membrane update (Euler).
      pop.v[k] = pop.v[k] + dt * (pop.v[k] - p.el - p.r * pop.i[k]) / p.tm
      // Fire?
      if pop.v[k] >= p.vt {
        pop.fire[k] = true
        pop.v[k] = p.vr
        pop.fire_t.push(k)
        pop.fire_t_delta[k] = pop.fire_t_delta[k] + 1
        pop.tabs[k] = pop.spike.tabs_const.to_int()  // refractory ticks
      }
    }
    // Single-exp synapse decay (matches Julia SingleExpSynapse update).
    pop.ge[k] = pop.ge[k] + pop.glu[k]
    pop.ge[k] = pop.ge[k] + dt * (-pop.ge[k] / p.tau_e)
    pop.gi[k] = pop.gi[k] + pop.gaba[k]
    pop.gi[k] = pop.gi[k] + dt * (-pop.gi[k] / p.tau_i)
    pop.glu[k] = 0.0F
    pop.gaba[k] = 0.0F
    k = k + 1
  }
  let _ = dt
}