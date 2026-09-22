// Morris-Lecar neuron — bit-exact port of
// SNNModels.jl/src/populations/morrislecar.jl.
//
// Julia reference:
//   v = -52.14, w = 0.2 (constant init)
//   m_ss = 0.5 * (1 + tanh((v - V1) / V2))
//   n_ss = 0.5 * (1 + tanh((v - V3) / V4))
//   τ   = 1 / (ϕ * cosh((v - V3) / (2 * V4)))
//
//   integrate! (per step):
//     v[i] += dt / Cm * MorrisLecar_dv(v, w, I, param)
//     w[i] += dt * MorrisLecar_dw(v, w, param)
//     v[i] += dt/Cm * (ge * (Ee - v) + gi * (Ei - v))
//     ge[i] += dt * -ge[i] / τe
//     gi[i] += dt * -gi[i] / τi
//     fire[i] = v > 20
//
// Float32 contract: every arithmetic uses `Float`; `tanh` uses `tanhf`.
// Unit normalisation (matching Julia):
//   Cm  = 6.69 pF = 6.69 (pF=1)
//   gl  = 0.5 nS = 0.5
//   gK  = 2 nS   = 2
//   gCa = 1.1 nS = 1.1
//   ϕ   = 25 Hz = 0.025 (Hz=0.001)

///|
/// MorrisLecarParameter — biophysical constants.
pub struct MorrisLecarParameter {
  cm : Float
  el : Float
  ek : Float
  eca : Float
  gl : Float
  gk : Float
  gca : Float
  tau_e : Float
  tau_i : Float
  v1 : Float
  v2 : Float
  v3 : Float
  v4 : Float
  phi : Float
  e_e : Float
  e_i : Float
}

///|
/// Default MorrisLecarParameter, matching Julia's `MorrisLecarParameter()`.
pub fn MorrisLecarParameter::new() -> MorrisLecarParameter {
  { cm: 6.69F, el: -50.0F, ek: -70.0F, eca: 100.0F,
    gl: 0.5F, gk: 2.0F, gca: 1.1F,
    tau_e: 5.0F, tau_i: 10.0F,
    v1: 30.0F, v2: 15.0F, v3: 0.0F, v4: 30.0F,
    phi: 25.0F * 0.001F,  // 25 Hz = 0.025
    e_e: 0.0F, e_i: -75.0F }
}

///|
/// Morris-Lecar neuron state — a population of N ML neurons.
pub struct MorrisLecar {
  param : MorrisLecarParameter
  n : Int
  v : Array[Float]
  w : Array[Float]
  fire : Array[Bool]
  i : Array[Float]
  ge : Array[Float]
  gi : Array[Float]
}

///|
/// Construct a new MorrisLecar population with `n` neurons.
pub fn MorrisLecar::new(n : Int, param : MorrisLecarParameter, _rng : Xoshiro) -> MorrisLecar {
  let v = Array::make(n, -52.14F)
  let w = Array::make(n, 0.2F)
  let fire : Array[Bool] = Array::make(n, false)
  let i : Array[Float] = Array::make(n, 0.0F)
  let ge = Array::make(n, 0.0F)
  let gi = Array::make(n, 0.0F)
  { param, n, v, w, fire, i, ge, gi }
}

///|
/// Update the MorrisLecar neuron state for one step.
/// Bit-exact port of `integrate!(p::MorrisLecar, param, dt)`.
pub fn step_ml(p : MorrisLecar, dt : Float) -> Unit {
  let n = p.n
  let p_ = p.param
  let cm = p_.cm
  let el = p_.el
  let ek = p_.ek
  let eca = p_.eca
  let gl = p_.gl
  let gk = p_.gk
  let gca = p_.gca
  let tau_e = p_.tau_e
  let tau_i = p_.tau_i
  let v1 = p_.v1
  let v2 = p_.v2
  let v3 = p_.v3
  let v4 = p_.v4
  let phi = p_.phi
  let e_e = p_.e_e
  let e_i = p_.e_i
  for i in 0..<n {
    let v = p.v[i]
    let w = p.w[i]
    // dv = I + gl*(El - v) + gCa*(ECa - v)*m_ss + gK*(EK - v)*w
    let m_ss = 0.5F * (1.0F + tanhf((v - v1) / v2))
    let dv = p.i[i] + gl * (el - v) + gca * (eca - v) * m_ss + gk * (ek - v) * w
    // dw = (n_ss - w) / τ
    let n_ss = 0.5F * (1.0F + tanhf((v - v3) / v4))
    // τ = 1 / (phi * cosh((v-V3)/(2*V4)))
    // cosh(x) = (exp(x) + exp(-x)) / 2; we can use 0.5*(expf(x) + expf(-x))
    let cosh_arg = (v - v3) / (2.0F * v4)
    let cosh_val = 0.5F * (expf(cosh_arg) + expf(-cosh_arg))
    let tau = if phi * cosh_val != 0.0F { 1.0F / (phi * cosh_val) } else { 0.0F }
    let dw = (n_ss - w) / tau
    p.v[i] = v + dt / cm * dv
    p.w[i] = w + dt * dw
  }
  for i in 0..<n {
    p.v[i] = p.v[i] + dt / cm * (p.ge[i] * (e_e - p.v[i]) + p.gi[i] * (e_i - p.v[i]))
  }
  for i in 0..<n {
    p.ge[i] = p.ge[i] + dt * (-p.ge[i] / tau_e)
    p.gi[i] = p.gi[i] + dt * (-p.gi[i] / tau_i)
  }
  for i in 0..<n {
    p.fire[i] = p.v[i] > 20.0F
  }
  ()
}
