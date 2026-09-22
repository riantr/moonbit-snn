// compose! — port of SNNModels.jl's `compose(; kwargs...)`.
//
// Julia's compose takes named arguments and groups them by type:
//   pops[k] = v        for v in kwargs if v isa AbstractPopulation
//   conns[k] = v       for v in kwargs if v isa AbstractConnection
//   stimuli[k] = v     for v in kwargs if v isa AbstractStimulus
// Returns a Model struct that the sim! loop knows how to drive.
//
// In MoonBit, we don't have a dynamic `Dict`-of-types. Instead we
// provide a builder that takes a heterogeneous list of populations
// (via `AnyPop`), connections (via `SpikingSynapse`), and stimuli
// (via `AnyStim`). The `HeterogeneousModel` is what `sim_heterogeneous!`
// drives. This is the v0.4.0 bridge between per-type sim loops
// and the mixed-population examples.

///|
/// Heterogeneous model: populations, connections, and stimuli.
/// Mirrors SNN's `Model{pop, conn, stim}`.
pub(all) struct HeterogeneousModel {
  pops : Array[AnyPop]
  conns : Array[SpikingSynapse]
  stims : Array[AnyStim]
  // Shared time tracker so the sim loop can advance it.
  time : Time
  // Monitors (optional). Driven in the inner loop so users can plot
  // :v, :fire, etc. across the simulation.
  monitors : Array[Monitor]
  // STDP entries — one per synapse that should be updated with a
  // plasticity rule each step. Empty by default (no plasticity).
  // Each entry's `conn_index` selects which synapse to update.
  // Mutating `t_now` happens inside step_heterogeneous.
  stdp_entries : Array[STDPEntryKind]
  // STP entries — one per synapse that should be updated with a
  // short-term plasticity rule (Markram) each step. Run BEFORE
  // forward so that the ρ scaling is fresh when spikes propagate.
  stp_entries : Array[STPEntryKind]
}

///|
/// Heterogeneous stimulus wrapper. Currently we have:
///   - `PoissonIF_` for Poisson spike-train input onto :ge/:gi
///   - `PoissonLayer_` for PoissonLayer (N independent Poisson sources
///     projecting sparsely to an IF population with optional Normal
///     weights) — mirrors Julia's `Stimulus(param, E, :ge, conn=...)`
///   - `BalancedIF_` for BalancedStimulus (feedback-driven inhomogeneous
///     Poisson source with low-pass noise and rate adaptation) — mirrors
///     Julia's `BalancedStimulus(E, :ge, :gi; param=BalancedParameter())`.
///   - `CurrentIF_` for direct current injection into an IF population
///   - `CurrentArr_` for direct current injection into a raw `i` array
///     (works for AdEx, IZ, HH, Poisson, etc.)
///   - `TimedStim_` for spike-time stimulus (exact spike times)
/// More variants (CurrentVariable, etc.) can be added later.
pub(all) enum AnyStim {
  PoissonIF_(PoissonStimulusIF)
  PoissonLayer_(PoissonLayerStimulus)
  BalancedIF_(BalancedStimulus)
  CurrentIF_(CurrentStimulusIF)
  CurrentArr_(CurrentStimulusArray)
  TimedStim_(SpikeTimeStimulus, Float)  // stimulus + weight
}

///|
/// Dispatch one stimulate! call based on the enum variant.
pub fn stimulate_any(s : AnyStim, t : Time, dt : Float) -> Unit {
  match s {
    PoissonIF_(x) => stimulate_if(x, get_time(t), dt)
    PoissonLayer_(x) => stimulate_layer(x, get_time(t), dt)
    BalancedIF_(x) => stimulate_balanced(x, get_time(t), dt)
    CurrentIF_(x) => stimulate_current_if(x)
    CurrentArr_(x) => stimulate_current_array(x)
    TimedStim_(x, w) => stimulate_spiketime(x, get_time(t), w)
  }
}

///|
/// Construct a heterogeneous model from populations, connections,
/// and (optionally) stimuli and monitors. Time starts at 0 with
/// dt=0.125F.
pub fn compose(
  pops : Array[AnyPop],
  conns : Array[SpikingSynapse],
  stims? : Array[AnyStim] = [],
  monitors? : Array[Monitor] = [],
  stdp? : Array[STDPEntryKind] = [],
  stp? : Array[STPEntryKind] = [],
) -> HeterogeneousModel {
  { pops, conns, stims, time: Time::new(), monitors, stdp_entries: stdp,
    stp_entries: stp }
}

///|
/// Drive a heterogeneous model for one timestep. Order matches SNN's
/// `sim!` inner loop:
///   1. stimulate! all input sources
///   2. forward! all connections (propagate pre-synaptic spikes)
///   3. apply STDP! (mutate weights for plasticity-bearing synapses)
///   4. integrate! each population
///   5. record! all monitors
///   6. update_time!
pub fn step_heterogeneous(m : HeterogeneousModel, dt : Float) -> Unit {
  // 1. stimulate! (apply external inputs to post-synaptic receptors)
  for s in m.stims {
    stimulate_any(s, m.time, dt)
  }
  // 2a. deliver pending spikes (delays scheduled in prior steps)
  for c in m.conns {
    deliver_pending_synapse(c, get_time(m.time))
  }
  // 2b. update STP traces — refresh per-connection ρ before forward
  // so that Markram scaling reflects current u/x state.
  for entry in m.stp_entries {
    match entry {
      MarkramSTP_(e) => {
        let syn = m.conns[e.conn_index]
        markram_stp_step(syn, e.vars, e.param, get_time(m.time))
      }
      MarkramSTPHet_(e) => {
        let syn = m.conns[e.conn_index]
        markram_stp_step_het(syn, e.vars, e.param, get_time(m.time))
      }
      MarkramSTPTimestep_(e) => {
        let syn = m.conns[e.conn_index]
        markram_stp_step_timestep(syn, e.vars, e.param, get_time(m.time), dt)
      }
    }
  }
  // 2c. forward! propagate spikes (no-delay: immediate; with delays:
  // schedule for future delivery)
  for c in m.conns {
    forward_synapse(c, get_time(m.time))
  }
  // 3. apply STDP! (plasticity mutates weights in-place)
  for entry in m.stdp_entries {
    match entry {
      Gerstner_(e) => {
        let syn = m.conns[e.conn_index]
        stdp_step(
          syn.matrix.vals,
          syn.pre.fire,
          syn.post.fire,
          syn.matrix.colptr,
          syn.matrix.rowptr,
          e.vars,
          e.param,
          e.t_now[0],
          dt,
        )
        e.t_now[0] = e.t_now[0] + dt
      }
      MexicanHat_(e) => {
        let syn = m.conns[e.conn_index]
        stdp_mexican_hat_step(
          syn.matrix.vals,
          syn.pre.fire,
          syn.post.fire,
          syn.matrix.colptr,
          syn.matrix.rowptr,
          e.tpre,
          e.tpost,
          e.param,
          dt,
        )
        e.t_now[0] = e.t_now[0] + dt
      }
      AntiSymmetric_(e) => {
        let syn = m.conns[e.conn_index]
        stdp_antisymmetric_step(
          syn.matrix.vals,
          syn.pre.fire,
          syn.post.fire,
          syn.matrix.colptr,
          syn.matrix.rowptr,
          e.vars,
          e.param,
          dt,
        )
        e.t_now[0] = e.t_now[0] + dt
      }
      Confavreux2025_(e) => {
        let syn = m.conns[e.conn_index]
        stdp_confavreux_step(
          syn.matrix.vals,
          syn.pre.fire,
          syn.post.fire,
          syn.matrix.colptr,
          syn.matrix.rowptr,
          e.vars,
          e.param,
          e.t_now[0],
          dt,
        )
        e.t_now[0] = e.t_now[0] + dt
      }
      IstdpRate_(e) => {
        let syn = m.conns[e.conn_index]
        istdp_rate_step(
          syn.matrix.vals,
          syn.pre.fire,
          syn.post.fire,
          syn.matrix.colptr,
          syn.matrix.rowptr,
          e.vars,
          e.param,
          e.t_now[0],
          dt,
        )
        e.t_now[0] = e.t_now[0] + dt
      }
      IstdpPotential_(e) => {
        let syn = m.conns[e.conn_index]
        istdp_potential_step(
          syn.matrix.vals,
          syn.pre.fire,
          syn.post.fire,
          syn.matrix.colptr,
          syn.matrix.rowptr,
          syn.post.v,
          e.vars,
          e.param,
          e.t_now[0],
          dt,
        )
        e.t_now[0] = e.t_now[0] + dt
      }
      Symmetric_(e) => {
        let syn = m.conns[e.conn_index]
        stdp_symmetric_step(
          syn.matrix.vals,
          syn.pre.fire,
          syn.post.fire,
          syn.matrix.colptr,
          syn.matrix.rowptr,
          e.vars,
          e.param,
          e.t_now[0],
          dt,
        )
        e.t_now[0] = e.t_now[0] + dt
      }
      CaPlasticity_(e) => {
        let syn = m.conns[e.conn_index]
        ca_plasticity_step(
          syn.matrix.vals,
          syn.pre.fire,
          syn.post.fire,
          syn.matrix.colptr,
          syn.matrix.rowptr,
          e.vars,
          e.param,
          e.t_now[0],
          dt,
        )
        e.t_now[0] = e.t_now[0] + dt
      }
    }
  }
  // 4. integrate! each population
  for p in m.pops {
    integrate_any(p, dt)
  }
  // 5. record! monitors
  for mon in m.monitors {
    record_one(mon, get_time(m.time))
  }
  // 6. advance time
  update_time(m.time, dt)
}

///|
/// Run the heterogeneous model for `duration` ms starting at t=0.
pub fn heterogeneous_sim_for(m : HeterogeneousModel, duration : Float) -> Unit {
  let dt = 0.125F
  let steps : Int = (duration / dt).to_int()
  for _ in 0..<steps {
    step_heterogeneous(m, dt)
  }
}

///|
/// Return the current simulation time (ms) of the model.
pub fn get_time_heterogeneous(m : HeterogeneousModel) -> Float {
  get_time(m.time)
}

///|
/// Reset the model's simulation clock back to 0. Does not clear
/// monitors or weight values — only the time tracker.
pub fn reset_time_heterogeneous(m : HeterogeneousModel) -> Unit {
  reset_time(m.time)
}
