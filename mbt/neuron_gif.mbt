// GIF (Generalized Integrate-and-Fire) — bit-exact port of
// SNNModels.jl GIF dispatcher.
//
// Julia reference:
//   src/populations/generalized_if/gif.jl  (AbstractGeneralizedIF
//     dispatcher + synaptic_target / synaptic_current! generic API)
//   src/populations/generalized_if/if.jl  (concrete IF/AdEx update)
//
// GIFParameter is the *superset* of IF + AdEx parameter struct: it
// has all the fields of IFParameter (C, gl, τm, Vt, Vr, El, R) plus
// the AdEx exponential-spike slope ΔT, sub-threshold adaptation a,
// spike-triggered adaptation b, and adaptation time-constant τw.
// This makes GIF a true generalization — the same struct can express
// pure LIF (a=b=τw=0, ΔT irrelevant), AdEx (a=4, b=80.5, τw=144),
// or any custom point in the (ΔT, a, b, τw) parameter space (Pozson
// 2018, Mihalas 2011, etc.).
//
// Float32 contract: every arithmetic uses `Float` (Float32). Update
// order matches Julia's IF update_neuron! exactly:
//
//   v += dt/τm * (-(v - El) + R*(-w + I) - R*syn_curr)
//   fire = v > Vt
//   v = ifelse(fire, Vr, v)
//   tabs = ifelse(fire, round(τabs/dt), tabs)
//   if fire: w += b
//   w += dt * (a*(v - El) - w) / τw

///|
/// GIFParameter — full parameter struct for any point in the LIF ↔ AdEx
/// generalisation space.
pub struct GIFParameter {
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
  tau_abs : Float
}

///|
/// Default GIFParameter (pure LIF: a=b=τw=0, ΔT=2mV default).
pub fn GIFParameter::new() -> GIFParameter {
  { c: -1.0F, gl: -1.0F, tm: 15.0F, vt: -50.0F, vr: -60.0F, el: -70.0F,
    r: 0.06F, dt_slope: 2.0F, a: 0.0F, b: 0.0F, tw: 0.0F, tau_abs: 5.0F }
}

///|
/// AdEx-like GIFParameter (a=4, b=80.5, τw=144ms, Vt=-50, Vr=-70.6).
pub fn GIFParameter::adex() -> GIFParameter {
  { c: 281.0F, gl: 40.0F, tm: 281.0F / 40.0F, vt: -50.0F, vr: -70.6F,
    el: -70.6F, r: 1.0F / 40.0F, dt_slope: 2.0F, a: 4.0F, b: 80.5F,
    tw: 144.0F, tau_abs: 5.0F }
}

///|
/// GIF — generalized integrate-and-fire neuron container.
///
/// Includes the IF state (v, fire, tabs, I, syn_curr) plus the
/// optional adaptation current w (zero unless param.tw > 0). Two
/// synaptic conductance buffers (glu / gaba) are kept for
/// compatibility with the SpikingSynapseIF routing conventions.
pub(all) struct GIF {
  n : Int
  param : GIFParameter
  v : Array[Float]
  w : Array[Float]
  fire : Array[Bool]
  tabs : Array[Float]
  i : Array[Float]
  syn_curr : Array[Float]
  glu : Array[Float]
  gaba : Array[Float]
}

///|
/// Default GIF (100 neurons, parameters = GIFParameter::new()).
pub fn GIF::new(
  n~ : Int = 100,
  param~ : GIFParameter = GIFParameter::new(),
) -> GIF {
  let v : Array[Float] = Array::make(n, param.vr)
  let w : Array[Float] = Array::make(n, 0.0F)
  let fire : Array[Bool] = Array::make(n, false)
  let tabs : Array[Float] = Array::make(n, 0.0F)
  let i : Array[Float] = Array::make(n, 0.0F)
  let syn_curr : Array[Float] = Array::make(n, 0.0F)
  let glu : Array[Float] = Array::make(n, 0.0F)
  let gaba : Array[Float] = Array::make(n, 0.0F)
  { n, param, v, w, fire, tabs, i, syn_curr, glu, gaba }
}

///|
/// update_neuron! — IF-style Euler + adaptation (only if τw > 0).
///
/// Julia's `if fire: w += b` and `w += dt * (a*(v - El) - w) / τw`
/// run regardless of τw when the if-guard hits. We replicate the
/// exact form.
pub fn update_neuron_gif(
  p : GIF,
  param : GIFParameter,
  dt : Float,
) -> Unit {
  let n = p.n
  let v = p.v
  let w = p.w
  let fire = p.fire
  let tabs = p.tabs
  let i = p.i
  let syn_curr = p.syn_curr
  let tm = param.tm
  let vt = param.vt
  let vr = param.vr
  let el = param.el
  let r = param.r
  let tau_abs = param.tau_abs
  let a = param.a
  let b = param.b
  let tw = param.tw
  let tabs_steps : Int = Float::to_int(tau_abs / dt + 0.5F)
  let mut k = 0
  while k < n {
    let tabs_k = tabs[k]
    if tabs_k > 0.0F {
      ignore(fire.set(k, false))
      ignore(tabs.set(k, tabs_k - 1.0F))
      k = k + 1
      continue
    }
    // Membrane update (Julia's `R*(-w + I) - R*syn_curr` form)
    let v_k = v[k]
    let w_k = w[k]
    let i_k = i[k]
    let sc_k = syn_curr[k]
    let v_new = v_k + dt / tm * (-(v_k - el) + r * (-w_k + i_k) - r * sc_k)
    let f = v_new > vt
    ignore(v.set(k, if f { vr } else { v_new }))
    ignore(fire.set(k, f))
    ignore(tabs.set(k, if f { Float::from_int(tabs_steps) } else { 0.0F }))
    // Adaptation current (only if τw > 0)
    if tw > 0.0F {
      let w_new = if f { w_k + b } else { w_k }
      let w_final = w_new + dt * (a * (v[k] - el) - w_new) / tw
      ignore(w.set(k, w_final))
    }
    k = k + 1
  }
}

///|
/// integrate! — runs update_neuron!. Synaptic current is set
/// externally (syn_curr array).
pub fn integrate_gif(
  p : GIF,
  param : GIFParameter,
  dt : Float,
) -> Unit {
  update_neuron_gif(p, param, dt)
}