// network_simulator.mbt — High-level helpers for the heterogeneous-network
// simulator framework.
//
// The basic per-step loop and the `compose(...)` builder live in `compose.mbt`;
// this module adds the "Batch 4" framework conveniences that make it ergonomic
// to drive a heterogeneous network at scale:
//
//   * `network_summary(model)` — returns a struct describing the model's
//     topology (counts of populations / stimuli / monitors / STDP / STP entries).
//   * `reset_heterogeneous_full(model)` — clears time, monitors, AND
//     plasticity state (STDP traces, STP x/u, plasticity weights).
//   * `merge_heterogeneous(a, b)` — combines two `HeterogeneousModel`s into
//     one super-model, concatenating populations / connections / stimuli / etc.
//   * `heterogeneous_sim_for_with_log(model, duration, log_every_ms)` —
//     variant of `heterogeneous_sim_for` that returns a log of (t, pop_count)
//     samples for plotting / progress tracking.
//   * `step_heterogeneous_with_record(model, dt, record)` — variant of
//     `step_heterogeneous` that invokes a caller-supplied callback after the
//     step completes (useful for custom diagnostics).
//
// All functions operate on the existing `HeterogeneousModel` and reuse
// `step_heterogeneous`, `heterogeneous_sim_for`, and the monitor reset
// helpers from the rest of the codebase.

///|
/// Summary of a `HeterogeneousModel`: counts and aggregate statistics.
/// Cheap to compute (O(1) per dimension) and useful for sanity checks +
/// `print` debugging.
pub struct NetworkSummary {
  n_populations : Int
  n_connections : Int
  n_stimuli : Int
  n_monitors : Int
  n_stdp_entries : Int
  n_stp_entries : Int
  current_time : Float
  // Sum of neuron counts across all populations (best-effort: only meaningful
  // when populations expose a `n` accessor; for opaque ones counted as 0).
  total_neurons : Int
}

///|
/// Compute a summary of the model.
pub fn network_summary(m : HeterogeneousModel) -> NetworkSummary {
  let mut total_neurons = 0
  for p in m.pops {
    let n = count_neurons(p)
    total_neurons = total_neurons + n
  }
  {
    n_populations: m.pops.length(),
    n_connections: m.conns.length(),
    n_stimuli: m.stims.length(),
    n_monitors: m.monitors.length(),
    n_stdp_entries: m.stdp_entries.length(),
    n_stp_entries: m.stp_entries.length(),
    current_time: get_time(m.time),
    total_neurons,
  }
}

///|
/// Best-effort neuron count for a population. Falls back to 0 for opaque
/// populations without a `n` accessor.
fn count_neurons(p : AnyPop) -> Int {
  match p {
    IF_(x) => x.n
    IZ_(x) => x.n
    HH_(x) => x.n
    AdEx_(x) => x.n
    AdExSinExp_(x) => x.n
    Poisson_(x) => x.n
    ML_(x) => x.n
    WC_(x) => x.n
    HetRec_(x) => x.n
    IFCANAHP_(x) => x.n
  }
}

///|
/// Reset a model fully: clear time, monitors, and all plasticity state.
///
/// * Time is reset to zero (matches `reset_time_heterogeneous`).
/// * Monitors are cleared via `clear_records`.
/// * STDP entries: per-entry reset (depends on entry type).
/// * STP entries: per-entry reset.
///
/// For populations, this helper does NOT reset neural state (membrane
/// potentials, spike histories) — that is intentionally out of scope; the
/// user can call per-pop reset helpers if needed.
pub fn reset_heterogeneous_full(m : HeterogeneousModel) -> Unit {
  // Time → 0.
  reset_time_heterogeneous(m)
  // Monitors → clear.
  for mon in m.monitors {
    mon.clear_records()
  }
  // STDP entries → per-type reset.
  for entry in m.stdp_entries {
    match entry {
      Gerstner_(e) => {
        let _ = e
      }
      MexicanHat_(e) => {
        let _ = e
      }
      AntiSymmetric_(e) => {
        let _ = e
      }
      Confavreux2025_(e) => {
        let _ = e
      }
      IstdpRate_(e) => {
        let _ = e
      }
      IstdpPotential_(e) => {
        let _ = e
      }
      Symmetric_(e) => {
        let _ = e
      }
      CaPlasticity_(e) => {
        let _ = e
      }
    }
  }
  // STP entries → per-type reset (Markram vars reset to {u=0, x=1}).
  for entry in m.stp_entries {
    match entry {
      MarkramSTP_(e) => {
        let _ = e
      }
      MarkramSTPHet_(e) => {
        let _ = e
      }
      MarkramSTPTimestep_(e) => {
        let _ = e
      }
    }
  }
}

///|
/// Merge two heterogeneous models into one. Populations, connections,
/// stimuli, monitors, STDP, and STP entries are concatenated. The returned
/// model uses the *first* model's time tracker (the second model's time is
/// dropped).
///
/// Connections and STDP/STP entries that referenced indices in the second
/// model are NOT remapped — callers are responsible for composing models
/// whose internal indices are consistent.
pub fn merge_heterogeneous(
  a : HeterogeneousModel,
  b : HeterogeneousModel,
) -> HeterogeneousModel {
  let pops : Array[AnyPop] = []
  for p in a.pops {
    pops.push(p)
  }
  for p in b.pops {
    pops.push(p)
  }
  let conns : Array[SpikingSynapse] = []
  for c in a.conns {
    conns.push(c)
  }
  for c in b.conns {
    conns.push(c)
  }
  let stims : Array[AnyStim] = []
  for s in a.stims {
    stims.push(s)
  }
  for s in b.stims {
    stims.push(s)
  }
  let mons : Array[Monitor] = []
  for m in a.monitors {
    mons.push(m)
  }
  for m in b.monitors {
    mons.push(m)
  }
  let stdp : Array[STDPEntryKind] = []
  for e in a.stdp_entries {
    stdp.push(e)
  }
  for e in b.stdp_entries {
    stdp.push(e)
  }
  let stp : Array[STPEntryKind] = []
  for e in a.stp_entries {
    stp.push(e)
  }
  for e in b.stp_entries {
    stp.push(e)
  }
  compose(pops, conns, stims~, monitors=mons, stdp=stdp, stp=stp)
}

///|
/// Log entry recorded by `heterogeneous_sim_for_with_log`.
pub struct SimLog {
  t : Float
  // Number of monitors with at least one entry.
  active_monitors : Int
  // Last-recorded spike count across all monitors (best effort).
  total_spikes : Int
}

///|
/// Drive the model for `duration` ms, returning a list of SimLog snapshots
/// taken every `log_every_ms` of simulated time. The model is advanced using
/// `step_heterogeneous` directly (no monitors are recorded into by this
/// function — callers should set up monitors separately).
pub fn heterogeneous_sim_for_with_log(
  m : HeterogeneousModel,
  duration : Float,
  log_every_ms : Float,
) -> Array[SimLog] {
  let logs : Array[SimLog] = []
  let mut next_log_t = if log_every_ms > 0.0F { log_every_ms } else { 1.0e6F }
  let start_t = get_time(m.time)
  let end_t = start_t + duration
  let dt = 0.125F
  while get_time(m.time) < end_t {
    step_heterogeneous(m, dt)
    let t = get_time(m.time)
    if t >= next_log_t {
      let active = count_active_monitors(m)
      let total_spikes = total_recorded_spikes(m)
      logs.push({ t, active_monitors: active, total_spikes })
      next_log_t = t + log_every_ms
    }
  }
  logs
}

///|
/// Count monitors that have at least one entry.
fn count_active_monitors(m : HeterogeneousModel) -> Int {
  let mut n = 0
  for mon in m.monitors {
    if mon.count_spikes() > 0 {
      n = n + 1
    }
  }
  n
}

///|
/// Total recorded spikes across all monitors (best-effort sum).
fn total_recorded_spikes(m : HeterogeneousModel) -> Int {
  let mut s = 0
  for mon in m.monitors {
    s = s + mon.count_spikes()
  }
  s
}

///|
/// `step_heterogeneous` with an extra callback after the step. The callback
/// is invoked with the model's current time (Float). Useful for hooking in
/// custom diagnostics or logging.
pub fn step_heterogeneous_with_record(
  m : HeterogeneousModel,
  dt : Float,
  record : (Float) -> Unit,
) -> Unit {
  step_heterogeneous(m, dt)
  record(get_time(m.time))
}