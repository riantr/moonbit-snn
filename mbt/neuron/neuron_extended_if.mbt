// ExtendedIF (Integrate-and-Fire with multi-receptor conductance) —
// bit-exact port of SNNModels.jl.
//
// Julia reference:
//   src/populations/generalized_if/if_extended.jl
//
// Differences from vanilla IF:
//   - 3 separate synaptic conductance buffers (g_Exc / g_PV / g_SST)
//     instead of one combined :ge / :gi receptor pair
//   - Per-synapse time-constants (τe vs τi — exc decays faster than
//     PV/SST inhibition)
//   - Dendritic interaction term `-α * g_Exc * g_SST * (E_e - v)` —
//     multiplicative shunting of the excitatory conductance by the
//     SST inhibitory conductance, modeling nonlinear dendritic
//     integration. Default α = 0 disables the interaction.
//
// Float32 contract: every arithmetic operation uses `Float` (Float32).
// Update order matches Julia's update_neuron! / update_synapses!
// exactly:
//
//   update_synapses!:
//     g_Exc[i] += dt * (-g_Exc[i] / τe)
//     g_PV[i]  += dt * (-g_PV[i]  / τi)
//     g_SST[i] += dt * (-g_SST[i] / τi)
//   update_neuron!:
//     dv = (gl*(El-v) + g_Exc*(E_e-v) + g_PV*(E_i-v) + g_SST*(E_i-v)
//           - α*g_Exc*g_SST*(E_e-v) + I) / Cm
//     v[i] += dt * dv
//     fire[i] = v[i] > Vt
//     v[i] = ifelse(fire[i], Vr, v[i])
//     tabs[i] = ifelse(fire[i], round(Int, τabs / dt), tabs[i])

///|
/// ExtendedIFParameter — biophysical constants of an LIF neuron with
/// 3 receptor-specific synaptic conductances + optional dendritic
/// interaction term.
///
/// Float32 fields match Julia's `@snn_kw struct ExtendedIFParameter`
/// defaults (Cm=250pF, Vt=-40mV, Vr=-65mV, El=-70mV, gl=10nS,
/// τe=6ms, τi=20ms, E_i=-75mV, E_e=0mV, τabs=5ms, α=0).
pub(all) struct ExtendedIFParameter {
  cm : Float
  vt : Float
  vr : Float
  el : Float
  gl : Float
  tau_e : Float
  tau_i : Float
  e_i : Float
  e_e : Float
  tau_abs : Float
  alpha : Float
}

///|
/// Default ExtendedIFParameter, matching Julia's `ExtendedIFParameter()`.
pub fn ExtendedIFParameter::new() -> ExtendedIFParameter {
  { cm: 250.0F, vt: -40.0F, vr: -65.0F, el: -70.0F, gl: 10.0F,
    tau_e: 6.0F, tau_i: 20.0F, e_i: -75.0F, e_e: 0.0F,
    tau_abs: 5.0F, alpha: 0.0F }
}

///|
/// ExtendedIFParameter with full custom values. Mirrors Julia's
/// `ExtendedIFParameter(; Cm, Vt, Vr, El, gl, τe, τi, E_i, E_e, τabs, α)`
/// keyword-only call style.
pub fn ExtendedIFParameter::custom(
  cm~ : Float,
  vt~ : Float,
  vr~ : Float,
  el~ : Float,
  gl~ : Float,
  tau_e~ : Float,
  tau_i~ : Float,
  e_i~ : Float,
  e_e~ : Float,
  tau_abs~ : Float,
  alpha~ : Float,
) -> ExtendedIFParameter {
  { cm, vt, vr, el, gl, tau_e, tau_i, e_i, e_e, tau_abs, alpha }
}

///|
/// ExtendedIF — multi-receptor conductance-based IF neuron.
///
/// State layout (1D arrays of length N):
///   v       — membrane potential (mV)
///   g_exc   — excitatory conductance (Exc AMPA-like, nS)
///   g_pv    — PV inhibition conductance (GABA_A fast, nS)
///   g_sst   — SST inhibition conductance (GABA_A slow, nS)
///   tabs    — absolute-refractory countdown (steps remaining)
///   fire    — spike flag at current step
///   i       — external input current (pA)
pub(all) struct ExtendedIF {
  n : Int
  param : ExtendedIFParameter
  v : Array[Float]
  g_exc : Array[Float]
  g_pv : Array[Float]
  g_sst : Array[Float]
  tabs : Array[Float]
  fire : Array[Bool]
  i : Array[Float]
}

///|
/// Default ExtendedIF (100 neurons, parameters = ExtendedIFParameter::new()).
/// v initialised to vr (we skip Julia's `vr .+ rand(N) .* (vt - vr)` RNG
/// init; tests verify the step functions, not the RNG-driven init).
pub fn ExtendedIF::new(
  n~ : Int = 100,
  param~ : ExtendedIFParameter = ExtendedIFParameter::new(),
) -> ExtendedIF {
  let v : Array[Float] = Array::make(n, param.vr)
  let g_exc : Array[Float] = Array::make(n, 0.0F)
  let g_pv : Array[Float] = Array::make(n, 0.0F)
  let g_sst : Array[Float] = Array::make(n, 0.0F)
  let tabs : Array[Float] = Array::make(n, 0.0F)
  let fire : Array[Bool] = Array::make(n, false)
  let i : Array[Float] = Array::make(n, 0.0F)
  { n, param, v, g_exc, g_pv, g_sst, tabs, fire, i }
}

///|
/// update_synapses! — exponential decay of the 3 conductance buffers.
pub fn update_synapses_extended_if(
  p : ExtendedIF,
  param : ExtendedIFParameter,
  dt : Float,
) -> Unit {
  let n = p.n
  let g_exc = p.g_exc
  let g_pv = p.g_pv
  let g_sst = p.g_sst
  let tau_e = param.tau_e
  let tau_i = param.tau_i
  let mut k = 0
  while k < n {
    ignore(g_exc.set(k, g_exc[k] + dt * (-g_exc[k] / tau_e)))
    ignore(g_pv.set(k, g_pv[k] + dt * (-g_pv[k] / tau_i)))
    ignore(g_sst.set(k, g_sst[k] + dt * (-g_sst[k] / tau_i)))
    k = k + 1
  }
}

///|
/// update_neuron! — Euler step with multi-receptor synaptic term
/// + optional dendritic interaction.
///
/// Julia's update order preserved bit-exactly:
///   1. tabs countdown (skip if refractory)
///   2. compute dv (synaptic term + dendritic α-gating)
///   3. v += dt * dv
///   4. fire = v > Vt
///   5. v = ifelse(fire, Vr, v)
///   6. tabs = ifelse(fire, round(τabs/dt), tabs)
pub fn update_neuron_extended_if(
  p : ExtendedIF,
  param : ExtendedIFParameter,
  dt : Float,
) -> Unit {
  let n = p.n
  let v = p.v
  let g_exc = p.g_exc
  let g_pv = p.g_pv
  let g_sst = p.g_sst
  let tabs = p.tabs
  let fire = p.fire
  let i = p.i
  let cm = param.cm
  let vt = param.vt
  let vr = param.vr
  let el = param.el
  let gl = param.gl
  let e_i = param.e_i
  let e_e = param.e_e
  let tau_abs = param.tau_abs
  let alpha = param.alpha
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
    let v_k = v[k]
    let g_e = g_exc[k]
    let g_p = g_pv[k]
    let g_s = g_sst[k]
    let i_k = i[k]
    let dv = (
        gl * (el - v_k) +
        g_e * (e_e - v_k) +
        g_p * (e_i - v_k) +
        g_s * (e_i - v_k) +
        -alpha * g_e * g_s * (e_e - v_k) +
        i_k
      ) / cm
    let v_new = v_k + dt * dv
    let f = v_new > vt
    ignore(v.set(k, if f { vr } else { v_new }))
    ignore(fire.set(k, f))
    ignore(tabs.set(k, if f { Float::from_int(tabs_steps) } else { 0.0F }))
    k = k + 1
  }
}

///|
/// integrate! — runs update_synapses! then update_neuron!. Mirrors
/// Julia's `integrate!(p, param, dt)` exactly.
pub fn integrate_extended_if(
  p : ExtendedIF,
  param : ExtendedIFParameter,
  dt : Float,
) -> Unit {
  update_synapses_extended_if(p, param, dt)
  update_neuron_extended_if(p, param, dt)
}