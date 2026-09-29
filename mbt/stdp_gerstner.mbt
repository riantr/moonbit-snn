// stdp_gerstner.mbt — STDP Gerstner 1996 pair-based exponential (v0.40.0).
//
// Gerstner, W., Kempter, R., van Hemmen, J. L., & Wagner, H. (1996).
// A neuronal learning rule for sub-millisecond temporal coding.
// Nature, 383(6595), 76–78.
//
// Each synapse carries a pre-synaptic trace tpre[j] and post-synaptic
// trace tpost[i]. On a pre-spike, the trace increases by A_pre; on a
// post-spike, the trace increases by A_post. Both traces decay
// exponentially with their own time constants τpre and τpost.
//
// On a post-spike: W[s] += A_pre · tpre[J[s]] (LTP via pre trace).
// On a pre-spike : W[s] += A_post · tpost[I[s]] (LTD via post trace).
//
// All updates clamped to [Wmin, Wmax]. Float32 throughout.

///|
/// STDP Gerstner parameters.
pub(all) struct StdpGerstnerParam {
  a_pre : Float    // LTP learning rate (post-spike reads pre-trace)
  a_post : Float   // LTD learning rate (pre-spike reads post-trace)
  tau_pre : Float  // pre-synaptic trace time constant (ms)
  tau_post : Float // post-synaptic trace time constant (ms)
  w_min : Float
  w_max : Float
}

///|
pub fn StdpGerstnerParam::new() -> StdpGerstnerParam {
  {
    a_pre: 0.01F,
    a_post: 0.01F,
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_min: 0.0F,
    w_max: 1.0F,
  }
}

///|
/// STDP Gerstner state (per-network traces).
pub struct StdpGerstnerState {
  n_pre : Int
  n_post : Int
  tpre : Array[Float]
  tpost : Array[Float]
  last_pre : Array[Float]
  last_post : Array[Float]
}

///|
pub fn StdpGerstnerState::new(n_pre : Int, n_post : Int) -> StdpGerstnerState {
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
/// One step of STDP Gerstner plasticity. `now` is the current
/// simulation time (ms). `fire_pre[j]` / `fire_post[i]` are boolean
/// spike indicators. `w` is the dense weight matrix indexed by
/// (i*n_pre + j) for post i, pre j; updated in-place.
pub fn stdp_gerstner_step(
  state : StdpGerstnerState,
  param : StdpGerstnerParam,
  fire_pre : Array[Bool],
  fire_post : Array[Bool],
  w : Array[Float],
  now : Float,
) -> Unit {
  let n_pre = state.n_pre
  let n_post = state.n_post
  let inv_tau_pre = 1.0F / param.tau_pre
  let inv_tau_post = 1.0F / param.tau_post
  // Update pre traces.
  for j in 0..<n_pre {
    let dt_pre = if now > state.last_pre[j] {
      now - state.last_pre[j]
    } else {
      0.0F
    }
    let decay = expf(-dt_pre * inv_tau_pre)
    if fire_pre[j] {
      state.tpre[j] = state.tpre[j] * decay + param.a_pre
      state.last_pre[j] = now
    } else {
      state.tpre[j] = state.tpre[j] * decay
    }
  }
  // Update post traces.
  for i in 0..<n_post {
    let dt_post = if now > state.last_post[i] {
      now - state.last_post[i]
    } else {
      0.0F
    }
    let decay = expf(-dt_post * inv_tau_post)
    if fire_post[i] {
      state.tpost[i] = state.tpost[i] * decay + param.a_post
      state.last_post[i] = now
    } else {
      state.tpost[i] = state.tpost[i] * decay
    }
  }
  // Weight updates.
  for i in 0..<n_post {
    for j in 0..<n_pre {
      let s = i * n_pre + j
      let mut delta = 0.0F
      if fire_post[i] {
        delta = delta + param.a_pre * state.tpre[j]
      }
      if fire_pre[j] {
        delta = delta + param.a_post * state.tpost[i]
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