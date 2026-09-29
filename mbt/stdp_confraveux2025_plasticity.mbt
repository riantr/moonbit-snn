// stdp_confraveux2025_plasticity.mbt — STDP Confavreux 2025 (v0.40.1).
//
// Variant of pair-based STDP (Gerstner-style trace pair) extended to
// support baseline-rate dependence:
//   On post-spike: W[s] += η · (ν · tpre[j] + β)        [post→pre direction]
//   On pre-spike : W[s] += η · (μ · tpost[i] + α)        [pre→post direction]
//
// The four constants α (post baseline), β (pre baseline), μ (post→pre
// amplitude), ν (pre→post amplitude) decouple the spike-timing
// component from a constant rate-dependent drift — useful for working
// with high background firing rates where classic STDP would over-
// depress.
//
// Reference: `STDP_confavreux_2025.jl` (in-tree ref). Note that the
// synapse model itself was ported earlier as `confraveux2025_synapse.mbt`
// (v0.10.81); this file ports the *plasticity rule* (a separate piece).

///|
pub(all) struct StdpConfavreux2025Param {
  eta : Float       // overall learning rate
  alpha : Float     // post-spike rate-dependent bias
  gamma : Float     // pre-spike rate-dependent bias
  mu : Float        // post→pre (LTP-side) amplitude
  nu : Float        // pre→post (LTD-side) amplitude
  tau_pre : Float
  tau_post : Float
  w_min : Float
  w_max : Float
}

///|
pub fn StdpConfavreux2025Param::new() -> StdpConfavreux2025Param {
  {
    eta: 0.01F,
    alpha: 0.0F,
    gamma: 0.0F,
    mu: 1.0F,
    nu: 1.0F,
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_min: 0.0F,
    w_max: 1.0F,
  }
}

///|
pub struct StdpConfavreux2025State {
  n_pre : Int
  n_post : Int
  tpre : Array[Float]
  tpost : Array[Float]
  last_pre : Array[Float]
  last_post : Array[Float]
}

///|
pub fn StdpConfavreux2025State::new(
  n_pre : Int,
  n_post : Int,
) -> StdpConfavreux2025State {
  {
    n_pre,
    n_post,
    tpre: Array::make(n_pre, 0.0F),
    tpost: Array::make(n_post, 0.0F),
    last_pre: Array::make(n_pre, 0.0F),
    last_post: Array::make(n_post, 0.0F),
  }
}

///|
pub fn stdp_confraveux2025_step(
  state : StdpConfavreux2025State,
  param : StdpConfavreux2025Param,
  fire_pre : Array[Bool],
  fire_post : Array[Bool],
  w : Array[Float],
  now : Float,
) -> Unit {
  let n_pre = state.n_pre
  let n_post = state.n_post
  let inv_tau_pre = 1.0F / param.tau_pre
  let inv_tau_post = 1.0F / param.tau_post
  // Trace update — Gerstner-style with the trace increment fixed at 1
  // (no A_pre/A_post scaling).
  for j in 0..<n_pre {
    let dt_pre = if now > state.last_pre[j] {
      now - state.last_pre[j]
    } else {
      0.0F
    }
    let decay = expf(-dt_pre * inv_tau_pre)
    if fire_pre[j] {
      state.tpre[j] = state.tpre[j] * decay + 1.0F
      state.last_pre[j] = now
    } else {
      state.tpre[j] = state.tpre[j] * decay
    }
  }
  for i in 0..<n_post {
    let dt_post = if now > state.last_post[i] {
      now - state.last_post[i]
    } else {
      0.0F
    }
    let decay = expf(-dt_post * inv_tau_post)
    if fire_post[i] {
      state.tpost[i] = state.tpost[i] * decay + 1.0F
      state.last_post[i] = now
    } else {
      state.tpost[i] = state.tpost[i] * decay
    }
  }
  // Weight updates with rate-dependent terms.
  for i in 0..<n_post {
    for j in 0..<n_pre {
      let s = i * n_pre + j
      let mut delta = 0.0F
      if fire_post[i] {
        delta = delta + param.eta * (param.nu * state.tpre[j] + param.gamma)
      }
      if fire_pre[j] {
        delta = delta + param.eta * (param.mu * state.tpost[i] + param.alpha)
      }
      let w_new = w[s] + delta
      if w_new < param.w_min {
        w[s] = param.w_min
      } else if w_new > param.w_max {
        w[s] = param.w_max
      } else {
        w[s] = w_new
      }
    }
  }
}