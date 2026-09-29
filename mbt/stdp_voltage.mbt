// stdp_voltage.mbt — voltage-dependent STDP (v0.40.2).
//
// Variant of pair-based STDP where the magnitude of LTP is gated by
// the post-synaptic membrane potential at the moment of the pre-spike:
// only above V_rest + ΔV does the synapse potentiate; below, it
// depresses. This models the experimental observation that STDP
// depends on the post-synaptic voltage (Clopath & Gerstner 2010).
//
// Update rule (simplified single-trace form):
//   On post-spike at time t:
//     for each synapse (i, j):
//       if tpre[j] > 0:
//         v_gap = v_post[i] - V_rest
//         if v_gap > ΔV:
//           ΔW = +A_LTP · tpre[j]
//         else if v_gap < -ΔV:
//           ΔW = -A_LTD · tpre[j]
//         else:
//           ΔW = 0
//       W[s] = clamp(W[s] + ΔW, Wmin, Wmax)
//
//   On every step: tpre[j] *= exp(-dt / τ_pre)   (exponential decay).
//
// We additionally use a post trace tpost[i] for the LTD-side
// dependence, so the rule has the form:
//   LTP: pre→post with v_post > V_rest + ΔV (potentiation above rest)
//   LTD: post→pre with v_post < V_rest - ΔV (depression below rest)

///|
pub(all) struct VStdpParam {
  a_ltp : Float    // LTP magnitude
  a_ltd : Float    // LTD magnitude
  v_rest : Float   // resting voltage (mV)
  delta_v : Float  // half-width of the no-change band (mV)
  tau_pre : Float
  tau_post : Float
  w_min : Float
  w_max : Float
}

///|
pub fn VStdpParam::new() -> VStdpParam {
  {
    a_ltp: 0.01F,
    a_ltd: 0.01F,
    v_rest: -70.0F,
    delta_v: 5.0F,
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_min: 0.0F,
    w_max: 1.0F,
  }
}

///|
pub struct VStdpState {
  n_pre : Int
  n_post : Int
  tpre : Array[Float]
  tpost : Array[Float]
  v_post : Array[Float]
}

///|
pub fn VStdpState::new(
  n_pre : Int,
  n_post : Int,
  v_post_init : Array[Float],
) -> VStdpState {
  {
    n_pre,
    n_post,
    tpre: Array::make(n_pre, 0.0F),
    tpost: Array::make(n_post, 0.0F),
    v_post: v_post_init,
  }
}

///|
/// One step of voltage-dependent STDP. `v_post` carries the current
/// membrane potential of each post-synaptic neuron. Spike indicators
/// drive the trace pair; the post voltage is what gates LTP vs LTD.
pub fn vstdp_clopath_step(
  state : VStdpState,
  param : VStdpParam,
  fire_pre : Array[Bool],
  fire_post : Array[Bool],
  w : Array[Float],
  dt : Float,
) -> Unit {
  let n_pre = state.n_pre
  let n_post = state.n_post
  let inv_tau_pre = dt / param.tau_pre
  let inv_tau_post = dt / param.tau_post
  // Update traces.
  for j in 0..<n_pre {
    if fire_pre[j] {
      state.tpre[j] = state.tpre[j] * expf(-inv_tau_pre) + 1.0F
    } else {
      state.tpre[j] = state.tpre[j] * expf(-inv_tau_pre)
    }
  }
  for i in 0..<n_post {
    if fire_post[i] {
      state.tpost[i] = state.tpost[i] * expf(-inv_tau_post) + 1.0F
    } else {
      state.tpost[i] = state.tpost[i] * expf(-inv_tau_post)
    }
  }
  // Weight updates gated by post voltage.
  let v_hi = param.v_rest + param.delta_v
  let v_lo = param.v_rest - param.delta_v
  for i in 0..<n_post {
    let v = state.v_post[i]
    for j in 0..<n_pre {
      let s = i * n_pre + j
      let mut delta = 0.0F
      if fire_post[i] && state.tpre[j] > 0.0F {
        // Pre→post timing. Gate on the post voltage.
        if v > v_hi {
          delta = delta + param.a_ltp * state.tpre[j]
        } else if v < v_lo {
          delta = delta - param.a_ltd * state.tpre[j]
        }
      }
      if fire_pre[j] && state.tpost[i] > 0.0F {
        // Post→pre timing. (Symmetric gating by v_post[i] also.)
        if v > v_hi {
          delta = delta + param.a_ltp * state.tpost[i]
        } else if v < v_lo {
          delta = delta - param.a_ltd * state.tpost[i]
        }
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