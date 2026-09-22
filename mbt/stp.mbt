// stp.mbt — Markram Short-Term Plasticity (STP) — bit-exact port of
// SNNModels.jl/src/connections/sparse_plasticity/STP.jl
//
// Markram et al. (1998) model of synaptic release dynamics:
//   u  : utilization of synaptic efficacy (release probability)
//   x  : fraction of available synaptic resources (1=full, 0=empty)
//   U  : baseline utilization (parameter; resting value of u)
//   τF : facilitation time constant — u recovers toward U with τF
//   τD : depression time constant — x recovers toward 1 with τD
//   ρ  : per-connection weight modifier; broadcasts _ρ[j] = u[j]*x[j]
//        to all outgoing edges from pre-neuron j.
//
// On a pre-spike at time T (Event-based update_traces!):
//   ΔT = max(0, T - last_spike[j])
//   last_spike[j] = T
//   u[j] = U - (U - u[j]) * exp(-ΔT / τF)        # recover toward U
//   x[j] = 1 - (1 - x[j]) * exp(-ΔT / τD)        # recover toward 1
//   _ρ[j] = u[j] * x[j]                          # effective scale
//   for s in colptr[j]:colptr[j+1]: ρ[s] = _ρ[j]  # broadcast to edges
//   u[j] += U * (1 - u[j])                       # facilitation bump
//   x[j] -= u[j] * x[j]                          # depression bump
//
// Initial state (no prior spike):
//   last_spike[j] = -Inf  (so first ΔT = +Inf, exp(-Inf)=0)
//   u[j] = U              (recovered)
//   x[j] = 1              (full)
//   After first spike: u[j] = U + U*(1-U) = U(2-U), x[j] = 0
//                       (first spike consumes all resources).
//   _ρ broadcast = U*1 = U, so first spike is delivered at scale U.
//
// The Timestep variant (MarkramSTPParameterTimestep) does an
// incremental Euler step on u and x every dt and is not implemented
// here yet — only the Event-based variant matches the canonical
// Markram 1998 formulation used by SNN.jl Mongillo2008 etc.

///|
/// MarkramSTPParameter — homogeneous STP parameters (same τD, τF, U
/// for every pre-neuron). Matches Julia's MarkramSTPParameterEvent.
pub(all) struct MarkramSTPParameter {
  // Time constant for depression (ms). Default: 200ms.
  tau_d : Float
  // Time constant for facilitation (ms). Default: 1500ms.
  tau_f : Float
  // Baseline utilization (release probability at rest). Default: 0.2.
  u : Float
  // Maximum weight clamp (carried for API parity; not enforced in
  // the Event-based update_traces! — Julia only uses Wmax/Wmin in
  // the Timestep variant).
  w_max : Float
  // Minimum weight clamp (carried for API parity; not enforced).
  w_min : Float
}

///|
/// Construct MarkramSTPParameter with SNN.jl defaults
/// (τD=200ms, τF=1500ms, U=0.2, Wmax=1.0, Wmin=0.0).
pub fn MarkramSTPParameter::new() -> MarkramSTPParameter {
  { tau_d: 200.0F, tau_f: 1500.0F, u: 0.2F, w_max: 1.0F, w_min: 0.0F }
}

///|
/// MarkramSTPVariables — per-pre-synaptic-neuron STP state.
pub(all) struct MarkramSTPVariables {
  // Pre- and post-synaptic neuron counts.
  n_pre : Int
  n_post : Int
  // Per-pre running utilization. Initially `param.u`.
  u : Array[Float]
  // Per-pre running resource availability (1 = full). Initially 1.
  x : Array[Float]
  // Per-pre effective weight scale _ρ = u * x. Initially `param.u`.
  // Exposed for inspection (matches Julia's `variables._ρ`).
  rho_pre : Array[Float]
  // Time of last spike per pre (initially -Inf).
  last_spike : Array[Float]
  // Single-element activation flag. Set to [false] to disable STP
  // for this connection without losing state. Matches Julia's
  // `MarkramSTPVariables.active::VBT`.
  active : Array[Bool]
}

///|
/// Allocate MarkramSTPVariables for a connection of size `n_pre` × `n_post`.
/// Initial state mirrors Julia's `plasticityvariables`:
///   u[j]  = param.u
///   x[j]  = 1.0F
///   ρ[j]  = param.u
///   last_spike[j] = -Inf
///   active = [true]
pub fn MarkramSTPVariables::new(
  n_pre : Int,
  n_post : Int,
  param : MarkramSTPParameter,
) -> MarkramSTPVariables {
  let u_arr : Array[Float] = Array::make(n_pre, param.u)
  let x_arr : Array[Float] = Array::make(n_pre, 1.0F)
  let rho_arr : Array[Float] = Array::make(n_pre, param.u)
  let ls_arr : Array[Float] = Array::make(n_pre, -1.0F / 0.0F)
  { n_pre, n_post, u: u_arr, x: x_arr, rho_pre: rho_arr, last_spike: ls_arr,
    active: [true] }
}

///|
/// MarkramSTPEntry — bundles a connection's STP rule with its mutable
/// per-step state so the compose layer can apply it automatically.
pub struct MarkramSTPEntry {
  // Connection identifier (index into HeterogeneousModel.conns).
  conn_index : Int
  // Mutable per-pre state (u, x, last_spike, rho_pre).
  vars : MarkramSTPVariables
  // STP rule (immutable).
  param : MarkramSTPParameter
}

///|
/// Construct a MarkramSTPEntry for the synapse at `conn_index` in
/// `HeterogeneousModel.conns`. Initialises per-pre state from `param`.
pub fn MarkramSTPEntry::new(
  conn_index : Int,
  n_pre : Int,
  n_post : Int,
  param? : MarkramSTPParameter = MarkramSTPParameter::new(),
) -> MarkramSTPEntry {
  {
    conn_index,
    vars: MarkramSTPVariables::new(n_pre, n_post, param),
    param,
  }
}

///|
/// Toggle STP on/off for this entry — mirrors Julia's
/// `set_STP!(s::SpikingSynapse, active)`. When `active=false`,
/// `markram_stp_step` becomes a no-op for this entry (traces don't
/// decay, weights use full ρ=1.0 instead of u*x). When `active=true`,
/// normal Markram STP resumes from whatever u/x state is currently
/// in `vars` (no reset).
///
/// Note: MoonBit identifiers can't contain `!`, so the trailing bang
/// is dropped. Semantic is identical.
pub fn MarkramSTPEntry::set_stp_active(
  entry : MarkramSTPEntry,
  active : Bool,
) -> Unit {
  if entry.vars.active.length() > 0 {
    entry.vars.active[0] = active
  }
}

///|
/// Heterogeneous-parameter variant of `set_stp_active` — toggles
/// the same `vars.active[0]` flag.
pub fn MarkramSTPEntryHet::set_stp_active(
  entry : MarkramSTPEntryHet,
  active : Bool,
) -> Unit {
  if entry.vars.active.length() > 0 {
    entry.vars.active[0] = active
  }
}

///|
/// Continuous-time (timestep) variant of `set_stp_active` —
/// toggles the same `vars.active[0]` flag.
pub fn MarkramSTPEntryTimestep::set_stp_active(
  entry : MarkramSTPEntryTimestep,
  active : Bool,
) -> Unit {
  if entry.vars.active.length() > 0 {
    entry.vars.active[0] = active
  }
}

///|
/// STPEntryKind — enum-dispatched wrapper around STP entry types.
/// Currently MarkramSTP_ (homogeneous) and MarkramSTPHet_ (per-pre
/// heterogeneous) are implemented; future variants can be added.
pub(all) enum STPEntryKind {
  MarkramSTP_(MarkramSTPEntry)
  MarkramSTPHet_(MarkramSTPEntryHet)
  MarkramSTPTimestep_(MarkramSTPEntryTimestep)
}

///|
/// Apply one Markram STP update step to the synapse `syn`, mutating
/// `vars` and broadcasting ρ to `syn.rho[]` for outgoing edges.
///
/// Math (matches Julia's update_traces! for MarkramSTPParameterEvent):
///   for j in eachindex(fireJ):
///     if fireJ[j]:
///       ΔT = max(0, t_now - last_spike[j])
///       last_spike[j] = t_now
///       u[j] = U - (U - u[j]) * exp(-ΔT / τF)
///       x[j] = 1 - (1 - x[j]) * exp(-ΔT / τD)
///       _ρ[j] = u[j] * x[j]
///       for s in colptr[j]:colptr[j+1]: ρ[s] = _ρ[j]
///       u[j] += U * (1 - u[j])
///       x[j] += -u[j] * x[j]
///
/// Requires `syn.rho` to be non-empty (caller must have called
/// `SpikingSynapse::init_rho(syn)` once after building the connectivity).
pub fn markram_stp_step(
  syn : SpikingSynapse,
  vars : MarkramSTPVariables,
  param : MarkramSTPParameter,
  t_now : Float,
) -> Unit {
  // Skip if STP is disabled (active[0] == false).
  if vars.active.length() > 0 && !vars.active[0] {
    return
  }
  let n_pre = vars.n_pre
  // Sanity: rho must be allocated; bail out gracefully if not.
  if syn.rho.length() == 0 {
    return
  }
  let tau_f = param.tau_f
  let tau_d = param.tau_d
  let u_baseline = param.u
  let mut j : Int = 0
  while j < n_pre {
    if syn.pre.fire[j] {
      // ΔT = max(0, t_now - last_spike[j]).
      let dt_pre = t_now - vars.last_spike[j]
      let dt_pre = if dt_pre < 0.0F { 0.0F } else { dt_pre }
      vars.last_spike[j] = t_now
      // u[j] = U - (U - u[j]) * exp(-ΔT / τF)
      let arg_f = -dt_pre / tau_f
      vars.u[j] = u_baseline - (u_baseline - vars.u[j]) * expf(arg_f)
      // x[j] = 1 - (1 - x[j]) * exp(-ΔT / τD)
      let arg_d = -dt_pre / tau_d
      vars.x[j] = 1.0F - (1.0F - vars.x[j]) * expf(arg_d)
      // _ρ[j] = u[j] * x[j]
      vars.rho_pre[j] = vars.u[j] * vars.x[j]
      // Broadcast to all outgoing connections of pre j.
      let start = syn.matrix.rowptr[j]
      let end = syn.matrix.rowptr[j + 1]
      let mut s = start
      while s < end {
        syn.rho[s] = vars.rho_pre[j]
        s = s + 1
      }
      // Update u and x for the next spike.
      vars.u[j] = vars.u[j] + u_baseline * (1.0F - vars.u[j])
      vars.x[j] = vars.x[j] + (-vars.u[j] * vars.x[j])
    }
    j = j + 1
  }
}

// =========================================================================
// MarkramSTPParameterHet — heterogeneous per-pre-synaptic-neuron STP
// parameters. Each pre-neuron j has its own (τD[j], τF[j], U[j]).
// Matches Julia's `MarkramSTPParameterHet{VFT = Vector{Float32}}`.
// =========================================================================

///|
/// MarkramSTPParameterHet — per-pre-neuron heterogeneous STP parameters.
/// τD, τF, U are all `Array[Float]` of length n_pre. Wmax/Wmin stay
/// scalar (weight clamping in the Event-based update is unused).
pub(all) struct MarkramSTPParameterHet {
  tau_d : Array[Float]  // per-pre depression time constant (ms)
  tau_f : Array[Float]  // per-pre facilitation time constant (ms)
  u : Array[Float]      // per-pre baseline utilization (0..1)
  w_max : Float         // weight clamp (carried for parity)
  w_min : Float         // weight clamp (carried for parity)
}

///|
/// Allocate MarkramSTPParameterHet with the same τD, τF, U for every
/// pre-neuron (homogeneous default = MarkramSTPParameter defaults).
pub fn MarkramSTPParameterHet::homogeneous(
  n_pre : Int,
) -> MarkramSTPParameterHet {
  {
    tau_d: Array::make(n_pre, 200.0F),
    tau_f: Array::make(n_pre, 1500.0F),
    u: Array::make(n_pre, 0.2F),
    w_max: 1.0F,
    w_min: 0.0F,
  }
}

///|
/// MarkramSTPVariables (shared by both homogeneous and heterogeneous
/// variants) — per-pre-synaptic-neuron STP state. The struct is
/// reused; the heterogeneous step function reads τD[j], τF[j], U[j]
/// from MarkramSTPParameterHet arrays instead of scalars.
pub struct MarkramSTPEntryHet {
  conn_index : Int
  vars : MarkramSTPVariables
  param : MarkramSTPParameterHet
}

///|
/// Construct a MarkramSTPEntryHet for the synapse at `conn_index` in
/// `HeterogeneousModel.conns`. Initialises per-pre state from the
/// per-neuron U array in `param`.
pub fn MarkramSTPEntryHet::new(
  conn_index : Int,
  n_pre : Int,
  n_post : Int,
  param : MarkramSTPParameterHet,
) -> MarkramSTPEntryHet {
  // Initialise u[j] = param.u[j] (per-pre), x[j] = 1, rho_pre[j] = u[j].
  let u_arr : Array[Float] = Array::make(n_pre, 0.0F)
  let x_arr : Array[Float] = Array::make(n_pre, 1.0F)
  let rho_arr : Array[Float] = Array::make(n_pre, 0.0F)
  let ls_arr : Array[Float] = Array::make(n_pre, -1.0F / 0.0F)
  let mut k : Int = 0
  while k < n_pre {
    u_arr[k] = param.u[k]
    rho_arr[k] = param.u[k]
    k = k + 1
  }
  {
    conn_index,
    vars: {
      n_pre,
      n_post,
      u: u_arr,
      x: x_arr,
      rho_pre: rho_arr,
      last_spike: ls_arr,
      active: [true],
    },
    param,
  }
}

///|
/// Apply one Markram STP update step using per-pre-neuron parameters
/// (τD[j], τF[j], U[j]). Mirrors Julia's
/// `update_traces!` for `MarkramSTPParameterHet`:
///   for j in eachindex(fireJ):
///     if fireJ[j]:
///       ΔT = max(0, t_now - last_spike[j])
///       last_spike[j] = t_now
///       u[j] = U[j] - (U[j] - u[j]) * exp(-ΔT / τF[j])
///       x[j] = 1 - (1 - x[j]) * exp(-ΔT / τD[j])
///       _ρ[j] = u[j] * x[j]
///       for s in colptr[j]:colptr[j+1]: ρ[s] = _ρ[j]
///       u[j] += U[j] * (1 - u[j])
///       x[j] -= u[j] * x[j]
///
/// Same `syn.rho` precondition as `markram_stp_step`.
pub fn markram_stp_step_het(
  syn : SpikingSynapse,
  vars : MarkramSTPVariables,
  param : MarkramSTPParameterHet,
  t_now : Float,
) -> Unit {
  if vars.active.length() > 0 && !vars.active[0] {
    return
  }
  let n_pre = vars.n_pre
  if syn.rho.length() == 0 {
    return
  }
  let mut j : Int = 0
  while j < n_pre {
    if syn.pre.fire[j] {
      let tau_d_j = param.tau_d[j]
      let tau_f_j = param.tau_f[j]
      let u_baseline_j = param.u[j]
      let dt_pre = t_now - vars.last_spike[j]
      let dt_pre = if dt_pre < 0.0F { 0.0F } else { dt_pre }
      vars.last_spike[j] = t_now
      let arg_f = -dt_pre / tau_f_j
      vars.u[j] = u_baseline_j - (u_baseline_j - vars.u[j]) * expf(arg_f)
      let arg_d = -dt_pre / tau_d_j
      vars.x[j] = 1.0F - (1.0F - vars.x[j]) * expf(arg_d)
      vars.rho_pre[j] = vars.u[j] * vars.x[j]
      let start = syn.matrix.rowptr[j]
      let end = syn.matrix.rowptr[j + 1]
      let mut s = start
      while s < end {
        syn.rho[s] = vars.rho_pre[j]
        s = s + 1
      }
      vars.u[j] = vars.u[j] + u_baseline_j * (1.0F - vars.u[j])
      vars.x[j] = vars.x[j] + (-vars.u[j] * vars.x[j])
    }
    j = j + 1
  }
}

///|
/// MarkramSTPParameterTimestep — timestep-driven Markram STP variant
/// from SNNModels.jl/src/connections/sparse_plasticity/STP.jl.
///
/// Differs from `MarkramSTPParameter` (event-based) in that the u, x
/// traces are updated continuously each simulation step (Euler
/// integration), regardless of whether a spike occurred. On a pre-
/// spike there is an additional discrete bump before the continuous
/// update.
///
/// Julia reference (MarkramSTPParameterTimestep defaults):
///   U = 0.5pF, τF = 750ms (facilitation), τD = 250ms (depression),
///   Wmax / Wmin carry-overs (unused in this pure-time variant).
///
/// Algorithm per step (mirrors Julia):
///   1. Spike bumps (if fireJ[j]):
///        u[j] += U * (1 - u[j])
///        x[j] += -u[j] * x[j]
///   2. Continuous-time Euler update (every j, every step):
///        u[j] += dt * (U - u[j]) / τF      # facilitation toward U
///        x[j] += dt * (1 - x[j]) / τD      # depression toward 1
///   3. Refresh rho_pre[j] = u[j] * x[j]
///   4. Broadcast rho_pre to syn.rho[s] for s in rowptr[j]:rowptr[j+1].
pub(all) struct MarkramSTPParameterTimestep {
  u : Float
  tau_f : Float
  tau_d : Float
  w_max : Float
  w_min : Float
}

///|
/// Defaults match Julia's MarkramSTPParameterTimestep (U=0.5,
/// tau_F=750ms, tau_D=250ms).
pub fn MarkramSTPParameterTimestep::new() -> MarkramSTPParameterTimestep {
  { u: 0.5F, tau_f: 750.0F, tau_d: 250.0F, w_max: 1.0e9F, w_min: -1.0e9F }
}

///|
/// MarkramSTPEntryTimestep — bundles a connection's
/// MarkramSTPParameterTimestep rule with the same `MarkramSTPVariables`
/// state used by the event-based variant. No `last_spike` bookkeeping
/// is needed (continuous-time updates don't depend on ΔT).
pub struct MarkramSTPEntryTimestep {
  conn_index : Int
  vars : MarkramSTPVariables
  param : MarkramSTPParameterTimestep
}

///|
/// Construct a MarkramSTPEntryTimestep. `vars` is allocated with
/// `u[j] = param.u`, `x[j] = 1.0`, `rho_pre[j] = param.u` (matches
/// `MarkramSTPVariables::new`).
pub fn MarkramSTPEntryTimestep::new(
  conn_index : Int,
  n_pre : Int,
  n_post : Int,
  param? : MarkramSTPParameterTimestep = MarkramSTPParameterTimestep::new(),
) -> MarkramSTPEntryTimestep {
  let u_arr : Array[Float] = Array::make(n_pre, param.u)
  let x_arr : Array[Float] = Array::make(n_pre, 1.0F)
  let rho_arr : Array[Float] = Array::make(n_pre, param.u)
  let ls_arr : Array[Float] = Array::make(n_pre, -1.0F / 0.0F)
  let vars : MarkramSTPVariables = {
    n_pre,
    n_post,
    u: u_arr,
    x: x_arr,
    rho_pre: rho_arr,
    last_spike: ls_arr,
    active: [true],
  }
  { conn_index, vars, param }
}

///|
/// One timestep Markram STP step.
///
/// Order of operations (mirrors Julia):
///   1. Spike bumps: if pre fire[j], apply `u += U*(1-u)` and
///      `x += -u*x`.
///   2. Continuous-time Euler: `u += dt*(U-u)/tau_f`,
///      `x += dt*(1-x)/tau_d` for every j.
///   3. Recompute `rho_pre[j] = u[j] * x[j]` and broadcast to
///      syn.rho[s] for s in rowptr[j]:rowptr[j+1].
pub fn markram_stp_step_timestep(
  syn : SpikingSynapse,
  vars : MarkramSTPVariables,
  param : MarkramSTPParameterTimestep,
  t_now : Float,
  dt : Float,
) -> Unit {
  let _ = t_now
  // Skip if STP is disabled.
  if vars.active.length() > 0 && !vars.active[0] {
    return
  }
  let n_pre = vars.n_pre
  // Sanity: rho must be allocated.
  if syn.rho.length() == 0 {
    return
  }
  let u_baseline = param.u
  let inv_tau_f : Float = 1.0F / param.tau_f
  let inv_tau_d : Float = 1.0F / param.tau_d
  // 1. Spike bumps.
  let mut j = 0
  while j < n_pre {
    if syn.pre.fire[j] {
      vars.u[j] = vars.u[j] + u_baseline * (1.0F - vars.u[j])
      vars.x[j] = vars.x[j] + (-vars.u[j] * vars.x[j])
    }
    j = j + 1
  }
  // 2. Continuous-time Euler update for every pre-neuron.
  j = 0
  while j < n_pre {
    vars.u[j] = vars.u[j] + dt * (u_baseline - vars.u[j]) * inv_tau_f
    vars.x[j] = vars.x[j] + dt * (1.0F - vars.x[j]) * inv_tau_d
    vars.rho_pre[j] = vars.u[j] * vars.x[j]
    // 4. Broadcast rho_pre to outgoing connections.
    let start = syn.matrix.rowptr[j]
    let end = syn.matrix.rowptr[j + 1]
    let mut s = start
    while s < end {
      syn.rho[s] = vars.rho_pre[j]
      s = s + 1
    }
    j = j + 1
  }
}
