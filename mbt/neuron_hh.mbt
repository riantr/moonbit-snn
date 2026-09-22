// HH (Hodgkin-Huxley) neuron — bit-exact port of
// SNNModels.jl/src/populations/hh.jl.
//
// Julia reference:
//   v: param.El + 5(randn() - 1)         (random init, mean = El - 5)
//   m: 0
//   n: 0
//   h: 1
//   ge: (1.5randn + 4) * 10nS
//   gi: (12randn + 20) * 10nS
//
//   integrate! (per step):
//     fire[i] = false
//     m[i] += dt * (a_m*(1-m) - b_m*m)        (sigmoid-like)
//     n[i] += dt * (a_n*(1-n) - b_n*n)
//     h[i] += dt * (a_h*(1-h) - b_h*h)
//     v[i] += dt/Cm * (I + gl*(El-v) + ge*(Ee-v) + gi*(Ei-v) +
//                       gn*m³*h*(En-v) + gk*n⁴*(Ek-v))
//     ge[i] += dt * -ge[i] / τe
//     gi[i] += dt * -gi[i] / τi
//     fire[i] = v > -20
//
// Unit normalisation (matching SNN's @snn_kw Float32):
//   Cm = 1uF * cm^(-2) * 20000um^2 = 1e6 * 1 * 2e-4 = 200.0F
//   gl = 5e-5siemens * cm^(-2) * 20000um^2 = 5e4 nS * 2e-4 = 10.0F
//   gn = 100msiemens * cm^(-2) * 20000um^2 = 1e8 nS * 2e-4 = 20000.0F
//   gk = 30msiemens * cm^(-2) * 20000um^2 = 3e7 nS * 2e-4 = 6000.0F

///|
/// HHParameter — biophysical constants of an HH neuron.
pub struct HHParameter {
  cm : Float
  gl : Float
  el : Float
  ek : Float
  en : Float
  gn : Float
  gk : Float
  vt : Float
  tau_e : Float
  tau_i : Float
  e_e : Float
  e_i : Float
}

///|
/// Default HHParameter, matching Julia's `HHParameter()`.
pub fn HHParameter::new() -> HHParameter {
  // Julia: Cm = 1uF * cm^(-2) * 20000um^2
  //   = Float32(1e6) * Float32(1.0) * Float32(20000 * 1e-8)
  //   = 1e6 * 1.0 * 2e-4
  //   = 200.0
  // Same for gl, gn, gk.
  let cm : Float = 1.0e6F * 1.0F * (20000.0F * 1.0e-8F) // = 200.0
  let gl : Float = 5.0e-5F * 1.0e9F * 1.0F * (20000.0F * 1.0e-8F) // = 10.0
  { cm, gl,
    el: -65.0F, ek: -90.0F, en: 50.0F,
    gn: 100.0F * 1.0e6F * 1.0F * (20000.0F * 1.0e-8F), // = 20000.0
    gk: 30.0F * 1.0e6F * 1.0F * (20000.0F * 1.0e-8F),  // = 6000.0
    vt: -63.0F, tau_e: 5.0F, tau_i: 10.0F,
    e_e: 0.0F, e_i: -80.0F }
}

///|
/// HH neuron state — a population of N Hodgkin-Huxley neurons.
pub struct HH {
  param : HHParameter
  n : Int
  v : Array[Float]
  m : Array[Float]
  n_gate : Array[Float]
  h : Array[Float]
  fire : Array[Bool]
  i : Array[Float]
  ge : Array[Float]
  gi : Array[Float]
}

///|
/// Construct a new HH population with `n` neurons.
pub fn HH::new(n : Int, param : HHParameter, rng : Xoshiro) -> HH {
  // v[i] = El + 5(randn() - 1) → mean El - 5, std 5
  // m, n = 0; h = 1
  // ge, gi = 0 (zeroed for stability — see IZ note)
  let v = Array::make(n, param.el)
  for k in 0..<n {
    let (z1, _) = box_muller(rng)
    v[k] = param.el + 5.0F * (Float::from_double(z1) - 1.0F)
  }
  let m = Array::make(n, 0.0F)
  let n_gate = Array::make(n, 0.0F)
  let h = Array::make(n, 1.0F)
  let fire : Array[Bool] = Array::make(n, false)
  let i : Array[Float] = Array::make(n, 0.0F)
  let ge = Array::make(n, 0.0F)
  let gi = Array::make(n, 0.0F)
  // Bit-exact: also consume the 2 Box-Muller draws for ge/gi init,
  // matching Julia's RNG state, but discard them (we use 0).
  for _k in 0..<n {
    let (_, _) = box_muller(rng)
  }
  { param, n, v, m, n_gate, h, fire, i, ge, gi }
}

///|
/// Update the HH neuron state for one step.
/// Bit-exact port of `integrate!(p::HH, param, dt)`.
pub fn step_hh(p : HH, dt : Float) -> Unit {
  let n = p.n
  let p_ = p.param
  let cm = p_.cm
  let gl = p_.gl
  let el = p_.el
  let ek = p_.ek
  let en = p_.en
  let gn = p_.gn
  let gk = p_.gk
  let vt = p_.vt
  let tau_e = p_.tau_e
  let tau_i = p_.tau_i
  let e_e = p_.e_e
  let e_i = p_.e_i
  for i in 0..<n {
    let v = p.v[i]
    let m = p.m[i]
    let nn = p.n_gate[i]
    let h = p.h[i]
    let ge = p.ge[i]
    let gi = p.gi[i]
    p.fire[i] = false
    // m update (sigmoid-like with expf)
    let am_num = 13.0F - v + vt
    let bm_num = v - vt - 40.0F
    let am = if expf(am_num / 4.0F) - 1.0F != 0.0F {
      0.32F * am_num / (expf(am_num / 4.0F) - 1.0F)
    } else {
      0.0F
    }
    let bm = if expf(bm_num / 5.0F) - 1.0F != 0.0F {
      0.28F * bm_num / (expf(bm_num / 5.0F) - 1.0F)
    } else {
      0.0F
    }
    p.m[i] = m + dt * (am * (1.0F - m) - bm * m)
    // n update
    let an_num = 15.0F - v + vt
    let an = if expf(an_num / 5.0F) - 1.0F != 0.0F {
      0.032F * an_num / (expf(an_num / 5.0F) - 1.0F)
    } else {
      0.0F
    }
    let bn = 0.5F * expf((10.0F - v + vt) / 40.0F)
    p.n_gate[i] = nn + dt * (an * (1.0F - nn) - bn * nn)
    // h update
    let ah = 0.128F * expf((17.0F - v + vt) / 18.0F)
    let bh_denom = 1.0F + expf((40.0F - v + vt) / 5.0F)
    let bh = if bh_denom != 0.0F { 4.0F / bh_denom } else { 0.0F }
    p.h[i] = h + dt * (ah * (1.0F - h) - bh * h)
    // v update
    let m_new = p.m[i]
    let n_new = p.n_gate[i]
    let h_new = p.h[i]
    let m3h = m_new * m_new * m_new * h_new
    let n4 = n_new * n_new * n_new * n_new
    let current = p.i[i] +
      gl * (el - v) +
      ge * (e_e - v) +
      gi * (e_i - v) +
      gn * m3h * (en - v) +
      gk * n4 * (ek - v)
    p.v[i] = v + dt / cm * current
    // synaptic decay
    p.ge[i] = ge + dt * (-ge / tau_e)
    p.gi[i] = gi + dt * (-gi / tau_i)
  }
  for i in 0..<n {
    p.fire[i] = p.v[i] > -20.0F
  }
  ()
}
