// istdp.mbt — Inhibitory STDP (iSTDP) rules from Vogels 2011.
//
// Port of SpikingNeuralNetworks.jl/src/connections/sparse_plasticity/
// iSTDP.jl. There are two variants:
//
//   - IstdpRate: inhibitory STDP with rate-based homeostasis.
//     `eta` learning rate, `r` target rate, `tau_y` STDP time constant,
//     `Wmax` / `Wmin` weight bounds.
//     Pre-spike:  W[s] += eta * (tpost[i] - 2 * r * tau_y)
//     Post-spike: W[s] += eta * tpre[j]
//     (clamped to [Wmin, Wmax])
//
//   - IstdpPotential: variant where the post-synaptic trace is driven
//     by the post-synaptic membrane potential (added in v0.10.34).
//
// Trace model: continuous-time Euler integration. Each step, the
// pre/post traces decay toward zero with time constant tau_y:
//   tpre[j] += dt * (-tpre[j]) / tau_y
// On a spike, the corresponding trace is bumped by 1.0F.
//
// v0.10.33 covers IstdpRate only; IstdpPotential is left for a
// follow-up version because its post trace depends on the
// post-synaptic membrane potential which is not in scope here.
//
// NOTE: MoonBit requires type names to start with uppercase. We
// rename Julia's `iSTDPRate` to `IstdpRate` (preserving the leading
// lowercase i → uppercase I, but the rest of the identifier matches
// the original).

///|
/// IstdpRate — Vogels 2011 inhibitory STDP with rate homeostasis.
///
/// Fields:
///   - eta : learning rate (Julia: 0.01pA, normalised to 0.01F)
///   - r   : target post-synaptic rate (Julia: 3Hz, internal units Hz*hz)
///   - tau_y : STDP time constant (Julia: 50ms)
///   - w_max / w_min : weight bounds (Julia: 243pF / 0.01pF)
pub(all) struct IstdpRate {
  eta : Float
  r : Float
  tau_y : Float
  w_max : Float
  w_min : Float
}

///|
/// Defaults match Julia's iSTDPRate (eta=0.01pA, r=3Hz, tau_y=50ms,
/// w_max=243pF, w_min=0.01pF). With @snn_kw's unit normalisation
/// (pA=1.0F, hz=0.001F, ms=1.0F, pF=1.0F), these map directly to the
/// Float32 values shown here.
pub fn IstdpRate::new() -> IstdpRate {
  {
    eta: 0.01F,
    r: 3.0F * hz, // 3 Hz = 3 * 0.001 = 0.003 (internal units rate/ms)
    tau_y: 50.0F,
    w_max: 243.0F,
    w_min: 0.01F,
  }
}

///|
/// IstdpRateVariables — per-connection plasticity state for IstdpRate.
///
/// Unlike `STDPVariables` (Gerstner), iSTDP keeps only the current
/// trace values (`tpre`, `tpost`). The trace model is continuous-time
/// Euler integration, so there is no `last_pre` / `last_post`
/// bookkeeping.
pub struct IstdpRateVariables {
  tpre : Array[Float]
  tpost : Array[Float]
}

///|
pub fn IstdpRateVariables::new(n_pre : Int, n_post : Int) -> IstdpRateVariables {
  {
    tpre: Array::make(n_pre, 0.0F),
    tpost: Array::make(n_post, 0.0F),
  }
}

///|
/// IstdpRateEntry — bundles a connection's IstdpRate rule with the
/// per-step state plus an internal `t_now` clock so the compose layer
/// can advance simulation time across step calls.
pub struct IstdpRateEntry {
  conn_index : Int
  n_pre : Int
  n_post : Int
  mut param : IstdpRate
  vars : IstdpRateVariables
  t_now : Array[Float]
}

///|
/// Construct an IstdpRateEntry. `vars` is zero-initialised; `t_now`
/// starts at 0.0F.
pub fn IstdpRateEntry::new(
  conn_index : Int,
  n_pre : Int,
  n_post : Int,
  param? : IstdpRate = IstdpRate::new(),
) -> IstdpRateEntry {
  {
    conn_index,
    n_pre,
    n_post,
    param,
    vars: IstdpRateVariables::new(n_pre, n_post),
    t_now: [0.0F],
  }
}

///|
/// Runtime swap of IstdpRate parameters. Preserves trace state.
pub fn IstdpRateEntry::change_plasticity(
  e : IstdpRateEntry,
  new_param : IstdpRate,
) -> Unit {
  e.param = new_param
}

///|
/// One step of the IstdpRate rule.
///
/// Trace model (continuous-time Euler, dt-step integration):
///   tpre[j]  += dt * (-tpre[j])  / tau_y
///   tpost[i] += dt * (-tpost[i]) / tau_y
/// On a pre spike:  tpre[j]  += 1
/// On a post spike: tpost[i] += 1
///
/// Weight update (per stored connection s = (j -> i)):
///   If pre fired:  w[s] += eta * (tpost[i] - 2 * r * tau_y)
///   If post fired: w[s] += eta * tpre[j]
///   Clamp w[s] to [w_min, w_max].
///
/// CSR layout (matches the rest of the SNN port): rowptr[j]..rowptr[j+1]
/// lists non-zero positions for row j (pre-neuron j); colptr[s] = i
/// (post-neuron). We walk rowptr[j] once per j and apply both the
/// pre-fire and post-fire contributions in the same loop (same fused
/// pattern as `stdp_step` and `stdp_confavreux_step`).
pub fn istdp_rate_step(
  w : Array[Float],
  pre_fire : Array[Bool],
  post_fire : Array[Bool],
  colptr : Array[Int],
  rowptr : Array[Int],
  vars : IstdpRateVariables,
  param : IstdpRate,
  t_now : Float,
  dt : Float,
) -> Unit {
  let _ = t_now
  let n_pre = pre_fire.length()
  let n_post = post_fire.length()
  let inv_tau_y : Float = 1.0F / param.tau_y
  // 1. Decay traces + spike bump.
  let mut j = 0
  while j < n_pre {
    vars.tpre[j] = vars.tpre[j] + dt * (-vars.tpre[j]) * inv_tau_y
    if pre_fire[j] {
      vars.tpre[j] = vars.tpre[j] + 1.0F
    }
    j = j + 1
  }
  let mut i = 0
  while i < n_post {
    vars.tpost[i] = vars.tpost[i] + dt * (-vars.tpost[i]) * inv_tau_y
    if post_fire[i] {
      vars.tpost[i] = vars.tpost[i] + 1.0F
    }
    i = i + 1
  }
  // 2. Walk all connections. For each connection (j -> i):
  //    if pre_fire[j]:  w[s] += eta * (tpost[i] - 2 * r * tau_y)
  //    if post_fire[i]: w[s] += eta * tpre[j]
  //    clamp w[s] to [w_min, w_max].
  j = 0
  while j < n_pre {
    let start = rowptr[j]
    let end = rowptr[j + 1]
    let pre_fired = pre_fire[j]
    let tpre_j = vars.tpre[j]
    let mut s = start
    while s < end {
      let post_idx = colptr[s]
      let post_fired = post_fire[post_idx]
      let tpost_i = vars.tpost[post_idx]
      if pre_fired {
        let dw = param.eta * (tpost_i - 2.0F * param.r * param.tau_y)
        w[s] = w[s] + dw
      }
      if post_fired {
        let dw = param.eta * tpre_j
        w[s] = w[s] + dw
      }
      // Clamp.
      if w[s] < param.w_min { w[s] = param.w_min }
      if w[s] > param.w_max { w[s] = param.w_max }
      s = s + 1
    }
    j = j + 1
  }
}
///|
/// IstdpPotential — Vogels 2011 inhibitory STDP with potential-based
/// post-synaptic trace.
///
/// Differs from IstdpRate in two ways:
///   1. `r` is replaced by `v0` (a reference potential in mV). The
///      pre-spike weight update becomes
///        w[s] += eta * (tpost[i] - v0)
///      where `tpost[i]` tracks the post-synaptic membrane potential
///      `v_post[i]` (low-pass filtered with time constant tau_y)
///      rather than spike count.
///   2. The default learning rate is smaller (eta=0.001pA) and the
///      trace time constant is larger (tau_y=200ms), reflecting the
///      longer memory of potential-based traces.
///
/// Defaults match Julia's iSTDPPotential (eta=0.001pA, v0=-50mV,
/// tau_y=200ms, w_max=243pF, w_min=0.01pF).
pub(all) struct IstdpPotential {
  eta : Float
  v0 : Float
  tau_y : Float
  w_max : Float
  w_min : Float
}

///|
pub fn IstdpPotential::new() -> IstdpPotential {
  {
    eta: 0.001F,
    v0: -50.0F,
    tau_y: 200.0F,
    w_max: 243.0F,
    w_min: 0.01F,
  }
}

///|
/// IstdpPotentialVariables — same shape as IstdpRateVariables: just
/// the running trace values `tpre` and `tpost`. The `tpost[i]` trace
/// here is updated each step to low-pass-filter `v_post[i]`, so the
/// step function takes the post-synaptic membrane potential as an
/// additional input.
pub struct IstdpPotentialVariables {
  tpre : Array[Float]
  tpost : Array[Float]
}

///|
pub fn IstdpPotentialVariables::new(
  n_pre : Int,
  n_post : Int,
) -> IstdpPotentialVariables {
  {
    tpre: Array::make(n_pre, 0.0F),
    tpost: Array::make(n_post, 0.0F),
  }
}

///|
/// IstdpPotentialEntry — bundles a connection's IstdpPotential rule
/// with per-step state plus an internal `t_now` clock.
pub struct IstdpPotentialEntry {
  conn_index : Int
  n_pre : Int
  n_post : Int
  mut param : IstdpPotential
  vars : IstdpPotentialVariables
  t_now : Array[Float]
}

///|
pub fn IstdpPotentialEntry::new(
  conn_index : Int,
  n_pre : Int,
  n_post : Int,
  param? : IstdpPotential = IstdpPotential::new(),
) -> IstdpPotentialEntry {
  {
    conn_index,
    n_pre,
    n_post,
    param,
    vars: IstdpPotentialVariables::new(n_pre, n_post),
    t_now: [0.0F],
  }
}

///|
/// Runtime swap of IstdpPotential parameters. Preserves trace state.
pub fn IstdpPotentialEntry::change_plasticity(
  e : IstdpPotentialEntry,
  new_param : IstdpPotential,
) -> Unit {
  e.param = new_param
}

///|
/// One step of the IstdpPotential rule.
///
/// Trace model (continuous-time Euler):
///   tpre[j]  += dt * (-tpre[j]) / tau_y
///   tpost[i] += dt * -(tpost[i] - v_post[i]) / tau_y
/// On a pre spike:  tpre[j]  += 1
/// On a post spike: tpost[i] += 1
///
/// Weight update (per stored connection s = (j -> i)):
///   If pre fired:  w[s] += eta * (tpost[i] - v0)
///   If post fired: w[s] += eta * tpre[j]
///   Clamp w[s] to [w_min, w_max].
///
/// `v_post` is the post-synaptic membrane potential array (length
/// n_post). Each element `v_post[i]` is in mV (internal units).
pub fn istdp_potential_step(
  w : Array[Float],
  pre_fire : Array[Bool],
  post_fire : Array[Bool],
  colptr : Array[Int],
  rowptr : Array[Int],
  v_post : Array[Float],
  vars : IstdpPotentialVariables,
  param : IstdpPotential,
  t_now : Float,
  dt : Float,
) -> Unit {
  let _ = t_now
  let n_pre = pre_fire.length()
  let n_post = post_fire.length()
  let inv_tau_y : Float = 1.0F / param.tau_y
  // 1. Decay traces + spike bump.
  //    tpre[j]  decays toward 0.
  //    tpost[i] decays toward v_post[i] (low-pass filter of membrane potential).
  let mut j = 0
  while j < n_pre {
    vars.tpre[j] = vars.tpre[j] + dt * (-vars.tpre[j]) * inv_tau_y
    if pre_fire[j] {
      vars.tpre[j] = vars.tpre[j] + 1.0F
    }
    j = j + 1
  }
  let mut i = 0
  while i < n_post {
    // Low-pass filter toward v_post[i].
    vars.tpost[i] = vars.tpost[i] + dt * (-(vars.tpost[i] - v_post[i])) * inv_tau_y
    if post_fire[i] {
      vars.tpost[i] = vars.tpost[i] + 1.0F
    }
    i = i + 1
  }
  // 2. Walk all connections. For each connection (j -> i):
  //    if pre_fire[j]:  w[s] += eta * (tpost[i] - v0)
  //    if post_fire[i]: w[s] += eta * tpre[j]
  //    clamp w[s] to [w_min, w_max].
  j = 0
  while j < n_pre {
    let start = rowptr[j]
    let end = rowptr[j + 1]
    let pre_fired = pre_fire[j]
    let tpre_j = vars.tpre[j]
    let mut s = start
    while s < end {
      let post_idx = colptr[s]
      let post_fired = post_fire[post_idx]
      let tpost_i = vars.tpost[post_idx]
      if pre_fired {
        let dw = param.eta * (tpost_i - param.v0)
        w[s] = w[s] + dw
      }
      if post_fired {
        let dw = param.eta * tpre_j
        w[s] = w[s] + dw
      }
      // Clamp.
      if w[s] < param.w_min { w[s] = param.w_min }
      if w[s] > param.w_max { w[s] = param.w_max }
      s = s + 1
    }
    j = j + 1
  }
}

///|
/// IstdpTime — Vogels 2011 inhibitory STDP time-based parameter type.
///
/// Mirrors Julia's `iSTDPTime{FT = Float32} <: iSTDPParameter` from
/// iSTDP.jl. This is a parameter-only struct — the Julia source defines
/// it but does not implement a separate step function for it (the
/// step rules for iSTDP.jl use iSTDPRate / iSTDPPotential only). The
/// struct exists so users can construct it as an LTPParam and the
/// `plasticity_params.jl` testset can verify its fields.
///
/// Fields match Julia defaults (eta=0.01pA, tau_y=50ms, w_max=243pF,
/// w_min=0.01pF).
pub(all) struct IstdpTime {
  eta : Float
  tau_y : Float
  w_max : Float
  w_min : Float
}

///|
pub fn IstdpTime::new() -> IstdpTime {
  { eta: 0.01F, tau_y: 50.0F, w_max: 243.0F, w_min: 0.01F }
}
