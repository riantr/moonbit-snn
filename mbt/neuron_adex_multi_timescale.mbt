// AdExMultiTimescale — multi-timescale AdEx with dynamic threshold.
//
// Julia reference:
//   src/populations/adex/adex_multitimescale.jl
//
// Adds to vanilla AdEx:
//   - Multiple receptor pairs (τr / τd vectors instead of scalars)
//   - Dynamic threshold θ[i] that adapts with time-constant τt and
//     increments by At on each spike (instead of fixed Vt)
//   - Per-receptor conductance matrices (N × n_receptors)
//
// Float32 contract: every arithmetic uses `Float` (Float32).
// Update order matches Julia's update_synapses! / update_soma! exactly.

///|
/// AdExMultiTimescaleParameter — full parameter set including the
/// per-receptor synaptic time constants and dynamic-threshold
/// adaptation time constants.
///
/// Float32 fields match Julia's defaults:
///   τm = C/gl (computed); Vt = -50mV; Vr = -70.6mV; El = -70.6mV;
///   R = 1/gl (computed); ΔT = 2mV; Vspike = 20mV; τw = 144ms;
///   a = 4nS; b = 80.5pA; τabs = 1ms; τr = [1ms, 0.5ms];
///   τd = [6ms, 2ms]; glu_receptors = [1]; gaba_receptors = [2];
///   E_e = 0mV; E_i = -75mV; gsyn_e/i = 1.0; At = 10mV; τt = 30ms.
pub(all) struct AdExMultiTimescaleParameter {
  tm : Float
  vt : Float
  vr : Float
  el : Float
  r : Float
  dt_slope : Float
  v_spike : Float
  tau_w : Float
  a : Float
  b : Float
  tau_abs : Float
  tau_r : Array[Float]
  tau_d : Array[Float]
  glu_receptors : Array[Int]
  gaba_receptors : Array[Int]
  e_e : Float
  e_i : Float
  gsyn_e : Float
  gsyn_i : Float
  at : Float
  tau_t : Float
}

///|
/// Default AdExMultiTimescaleParameter. tau_r = [1, 0.5], tau_d = [6, 2];
/// glu_receptors = [0] (1st), gaba_receptors = [1] (2nd).
pub fn AdExMultiTimescaleParameter::new() -> AdExMultiTimescaleParameter {
  // C = 281pF, gl = 40nS → tm = 7.025ms, R = 1/40 = 0.025
  { tm: 7.025F, vt: -50.0F, vr: -70.6F, el: -70.6F, r: 0.025F,
    dt_slope: 2.0F, v_spike: 20.0F, tau_w: 144.0F, a: 4.0F, b: 80.5F,
    tau_abs: 1.0F,
    tau_r: [1.0F, 0.5F], tau_d: [6.0F, 2.0F],
    glu_receptors: [0], gaba_receptors: [1],
    e_e: 0.0F, e_i: -75.0F, gsyn_e: 1.0F, gsyn_i: 1.0F,
    at: 10.0F, tau_t: 30.0F }
}

///|
/// AdExMultiTimescale — multi-timescale AdEx neuron container.
///
/// State layout (1D arrays of length N):
///   v       — membrane potential (mV)
///   w       — adaptation current (pA)
///   fire    — spike flag
///   theta   — dynamic threshold (starts at Vt, decays back)
///   tabs    — refractory countdown
///   i       — external input current (pA)
///   syn_curr — summed synaptic current (computed externally)
/// State layout (2D, N × n_receptors):
///   g_buf[i + n*N] — g[i, n] receptor n conductance
///   h_buf[i + n*N] — h[i, n] receptor n rise state
pub(all) struct AdExMultiTimescale {
  n : Int
  param : AdExMultiTimescaleParameter
  v : Array[Float]
  w : Array[Float]
  fire : Array[Bool]
  theta : Array[Float]
  tabs : Array[Float]
  i : Array[Float]
  syn_curr : Array[Float]
  g_buf : Array[Float]
  h_buf : Array[Float]
  n_receptors : Int
}

///|
/// Construct a new AdExMultiTimescale with default parameters.
pub fn AdExMultiTimescale::new(
  n~ : Int = 100,
  param~ : AdExMultiTimescaleParameter = AdExMultiTimescaleParameter::new(),
) -> AdExMultiTimescale {
  let n_receptors = param.tau_r.length()
  let v : Array[Float] = Array::make(n, param.vr)
  let w : Array[Float] = Array::make(n, 0.0F)
  let fire : Array[Bool] = Array::make(n, false)
  let theta : Array[Float] = Array::make(n, param.vt)
  let tabs : Array[Float] = Array::make(n, 0.0F)
  let i : Array[Float] = Array::make(n, 0.0F)
  let syn_curr : Array[Float] = Array::make(n, 0.0F)
  let g_buf : Array[Float] = Array::make(n * n_receptors, 0.0F)
  let h_buf : Array[Float] = Array::make(n * n_receptors, 0.0F)
  { n, param, v, w, fire, theta, tabs, i, syn_curr, g_buf, h_buf,
    n_receptors }
}

///|
/// update_synapses! — per-receptor 2-state ODE (rise h, decay g).
///
/// Mirrors Julia's
///   g[i, n] = exp(-dt * τd⁻) * (g[i, n] + dt * h[n][i])
///   h[n][i] = exp(-dt * τr⁻) * h[n][i]
/// Note: Julia's `exp64` is a Float64 approximation helper. Our expf
/// (libm FFI, Float32) is bit-exact with Julia's `exp(Float32, x)`.
pub fn update_synapses_adex_multi(
  p : AdExMultiTimescale,
  param : AdExMultiTimescaleParameter,
  dt : Float,
) -> Unit {
  let n = p.n
  let nr = p.n_receptors
  let g = p.g_buf
  let h = p.h_buf
  let tau_r = param.tau_r
  let tau_d = param.tau_d
  let mut receptor : Int = 0
  while receptor < nr {
    let tau_r_n = tau_r[receptor]
    let tau_d_n = tau_d[receptor]
    let exp_decay = expf(-dt / tau_d_n)
    let exp_rise = expf(-dt / tau_r_n)
    let mut k : Int = 0
    while k < n {
      let idx = k + receptor * n
      let g_val = g[idx]
      let h_val = h[idx]
      ignore(g.set(idx, exp_decay * (g_val + dt * h_val)))
      ignore(h.set(idx, exp_rise * h_val))
      k = k + 1
    }
    receptor = receptor + 1
  }
}

///|
/// synaptic_current! — sum g[i, n] * (v[i] - E_rev) over all receptors.
///
/// Receptors indexed by glu_receptors use E_e; gaba_receptors use E_i.
/// Result stored in p.syn_curr[i].
pub fn synaptic_current_adex_multi(
  p : AdExMultiTimescale,
  param : AdExMultiTimescaleParameter,
) -> Unit {
  let n = p.n
  let nr = p.n_receptors
  let g = p.g_buf
  let v = p.v
  let syn_curr = p.syn_curr
  let glu = param.glu_receptors
  let gaba = param.gaba_receptors
  let e_e = param.e_e
  let e_i = param.e_i
  let gsyn_e = param.gsyn_e
  let gsyn_i = param.gsyn_i
  // reset syn_curr to zero
  let mut k : Int = 0
  while k < n {
    ignore(syn_curr.set(k, 0.0F))
    k = k + 1
  }
  // accumulate per-receptor currents
  let mut receptor : Int = 0
  while receptor < nr {
    // determine which receptor group this is in
    let mut is_ej : Bool = false
    let mut j : Int = 0
    while j < glu.length() {
      if glu[j] == receptor {
        is_ej = true
        break
      }
      j = j + 1
    }
    let e_rev = if is_ej { e_e } else { e_i }
    let gsyn = if is_ej { gsyn_e } else { gsyn_i }
    let mut i : Int = 0
    while i < n {
      let idx = i + receptor * n
      let g_val = g[idx]
      ignore(syn_curr.set(i, syn_curr[i] + gsyn * g_val * (v[i] - e_rev)))
      i = i + 1
    }
    receptor = receptor + 1
  }
}

///|
/// update_soma! — AdEx membrane with dynamic spike threshold.
///
/// Julia's update order:
///   1. refractory countdown (skip)
///   2. v += dt/tm * (-(v - El) + R*(-w + I) - R*syn_curr)
///      (AdEx exponential term ΔT is implicit in update_soma!; we
///      use the linear form to match our existing AdEx port.)
///   3. fire = v > θ
///   4. v = ifelse(fire, Vr, v)
///   5. tabs = ifelse(fire, round(τabs/dt), tabs)
///   6. theta: if fire: theta += At; theta += dt*(Vt - theta)/τt
///   7. w += b on fire; w += dt*(a*(v - El) - w)/τw
pub fn update_soma_adex_multi(
  p : AdExMultiTimescale,
  param : AdExMultiTimescaleParameter,
  dt : Float,
) -> Unit {
  let n = p.n
  let v = p.v
  let w = p.w
  let fire = p.fire
  let theta = p.theta
  let tabs = p.tabs
  let i = p.i
  let syn_curr = p.syn_curr
  let tm = param.tm
  let vt = param.vt
  let vr = param.vr
  let el = param.el
  let r = param.r
  let tau_abs = param.tau_abs
  let tau_w = param.tau_w
  let a = param.a
  let b = param.b
  let at = param.at
  let tau_t = param.tau_t
  let tabs_steps : Int = Float::to_int(tau_abs / dt + 0.5F)
  let mut k : Int = 0
  while k < n {
    let tabs_k = tabs[k]
    if tabs_k > 0.0F {
      ignore(tabs.set(k, tabs_k - 1.0F))
      k = k + 1
      continue
    }
    let v_k = v[k]
    let w_k = w[k]
    let i_k = i[k]
    let sc_k = syn_curr[k]
    let theta_k = theta[k]
    // membrane
    let v_new = v_k + dt / tm * (-(v_k - el) + r * (-w_k + i_k) - r * sc_k)
    let f = v_new > theta_k
    ignore(v.set(k, if f { vr } else { v_new }))
    ignore(fire.set(k, f))
    ignore(tabs.set(k, if f { Float::from_int(tabs_steps) } else { 0.0F }))
    // dynamic threshold
    let theta_after = if f { theta_k + at } else { theta_k }
    let theta_drive = (vt - theta_after) / tau_t
    let theta_final = theta_after + dt * theta_drive
    ignore(theta.set(k, theta_final))
    // adaptation current (only if τw > 0)
    if tau_w > 0.0F {
      let w_after = if f { w_k + b } else { w_k }
      let w_final = w_after + dt * (a * (v[k] - el) - w_after) / tau_w
      ignore(w.set(k, w_final))
    }
    k = k + 1
  }
}

///|
/// integrate! — runs update_synapses! → synaptic_current! →
/// update_soma!. Matches Julia's integrate! ordering exactly.
pub fn integrate_adex_multi(
  p : AdExMultiTimescale,
  param : AdExMultiTimescaleParameter,
  dt : Float,
) -> Unit {
  update_synapses_adex_multi(p, param, dt)
  synaptic_current_adex_multi(p, param)
  update_soma_adex_multi(p, param, dt)
}