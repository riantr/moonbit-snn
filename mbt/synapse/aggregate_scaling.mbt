// aggregate_scaling.mbt — AggregateScaling infrastructure.
//
// Julia reference:
//   SNNModels.jl/src/connections/metaplasticity/aggregate_scaling.jl
//   SNNModels.jl/test/network/aggregate_scaling.jl
//
// HomeAggregate scaling rule: tracks a per-post-synaptic-neuron
// trace y[i] that bumps on fire[i] and decays with time constant tau_a;
// uses the trace to drive a slow homeostatic weight scaling toward
// a target rate Y[i]. Applied every tau time units.
//
// Provides:
//   - `AggregateScalingParameter` (τe, τa, τ, Y, Wmin, Wmax) matching
//     Julia's `AggregateScalingParameter`.
//   - `AggregateScaling` (N, param, synapses, Wt, WT, fire, y, μ).
//   - `AggregateScaling::new(post, synapses, param?)` initialises
//     WT[i] = sum of incoming weights, Wt = 0, fire = 0, y = 0,
//     μ = 0.
//   - `aggregate_scaling_forward(c, dt)` — updates y[i] (low-pass),
//     bumps on fire, drives WT[i] toward (1 - y[i]/Y[i]).
//   - `aggregate_scaling_plasticity(c, dt, t_now)` — every τ ms,
//     compute μ[i] = (WT[i] - Wmin) / Wt[i] and rescale weights:
//     W[s] = W[s] * μ[i] + Wmin.
//
// Bit-exact ordering matches Julia's `forward!` and `plasticity!`.

// =========================================================================
// AggregateScalingParameter
// =========================================================================

///|
/// AggregateScalingParameter — homeostatic scaling rule parameters.
/// Mirrors Julia's `@snn_kw struct AggregateScalingParameter`.
/// Defaults: τ=10ms, Wmin=0.5, Wmax=250. Required: τe, τa, Y.
pub struct AggregateScalingParameter {
  // plasticity interval (ms). Every τ time units, apply the
  // homeostatic weight rescaling.
  tau : Float
  // Homeostatic trace decay time constant (ms).
  tau_a : Float
  // WT[i] update time constant (ms).
  tau_e : Float
  // Per-post-neuron target rate Y[i] (Hz * hz = per-ms internal units).
  y : Array[Float]
  // Minimum per-synapse weight (nS in normalised units).
  w_min : Float
  // Maximum per-synapse weight (nS).
  w_max : Float
}

///|
/// Construct AggregateScalingParameter with explicit values.
/// `y : Array[Float]` is the per-post-neuron target rate (in
/// internal units = rate per ms). Use `y[k] = 10.0F * hz` for 10 Hz.
pub fn AggregateScalingParameter::new(
  tau~ : Float = 10.0F,
  tau_a~ : Float,
  tau_e~ : Float,
  y : Array[Float],
  w_min~ : Float = 0.5F,
  w_max~ : Float = 250.0F,
) -> AggregateScalingParameter {
  { tau, tau_a, tau_e, y, w_min, w_max }
}

///|
/// Convenience constructor: N post-synaptic neurons, all with the
/// same target rate (in Hz). Mirrors Julia's
/// `AggregateScalingParameter(N; rate=10Hz, ...)`.
pub fn AggregateScalingParameter::uniform(
  n : Int,
  rate_hz : Float,
  tau~ : Float = 10.0F,
  tau_a~ : Float = 100.0F,
  tau_e~ : Float = 100.0F,
  w_min~ : Float = 0.05F,
  w_max~ : Float = 250.0F,
) -> AggregateScalingParameter {
  // Convert Hz to internal units (Hz = 0.1 / ms since internal
  // rate is per ms). User supplies float Hz, we multiply by 0.1F.
  let rate_internal = rate_hz * 0.1F
  let y_arr : Array[Float] = Array::make(n, rate_internal)
  AggregateScalingParameter::new(
    tau~,
    tau_a~,
    tau_e~,
    y_arr,
    w_min~,
    w_max~,
  )
}

// =========================================================================
// AggregateScaling
// =========================================================================

///|
/// AggregateScaling — holds the per-iteration state needed to apply
/// the homeostatic rule. Mirrors Julia's
/// `@snn_kw struct AggregateScaling` (subset of fields).
///
/// fields:
///   - `param` : AggregateScalingParameter
///   - `n` : number of post-synaptic neurons
///   - `synapses` : list of SpikingSynapses whose weights are rescaled.
///     Each synapse is referenced via a `SynapseTarget` (same as
///     `SynapseNormalization` uses) so the rule can mutate the
///     weights in-place.
///   - `wt` : temporary per-post-neuron weight sum (recomputed each
///     plasticity call)
///   - `wt_total` : per-post-neuron homeostatic target sum (updated
///     in forward)
///   - `fire` : borrowed reference to the post-population's fire
///     array (read-only here; updated externally each step)
///   - `y` : per-post-neuron homeostatic trace
///   - `mu` : per-post-neuron plasticity multiplier (computed each
///     plasticity call)
///   - `last_plasticity_step` : counter used by the periodic
///     plasticity scheduler (every tau ms)
pub struct AggregateScaling {
  param : AggregateScalingParameter
  n : Int
  // SynapseTarget list (same as SynapseNormalization).
  targets : Array[SynapseTarget]
  // Per-post-neuron current weight sum (recomputed in plasticity).
  // Note: Array fields don't need `mut` in MoonBit — element
  // assignment `arr[i] = x` works regardless.
  wt : Array[Float]
  // Per-post-neuron homeostatic target sum (updated in forward).
  wt_total : Array[Float]
  // Per-post-neuron homeostatic trace (low-pass of fire).
  y : Array[Float]
  // Per-post-neuron plasticity multiplier (computed in plasticity).
  mu : Array[Float]
  // Step counter for the periodic plasticity scheduler.
  last_plasticity_step : Int
  // Step interval (steps between plasticity applications).
  plasticity_interval_steps : Int
}

///|
/// Construct AggregateScaling from a post-population and a list of
/// synapses (provided via `Array[SynapseTarget]`). `n` is the
/// number of post-synaptic neurons. Initialises WT[i] = sum of
/// incoming weights (matches Julia's constructor).
pub fn AggregateScaling::new(
  n : Int,
  targets : Array[SynapseTarget],
  param : AggregateScalingParameter,
) -> AggregateScaling {
  // Compute WT[i] = sum of incoming weights at construction.
  let wt_total : Array[Float] = Array::make(n, 0.0F)
  let mut t = 0
  while t < targets.length() {
    let tg = targets[t]
    let vals = tg.vals
    let rowptr = tg.rowptr
    let mut i = 0
    while i < n {
      let start = rowptr[i]
      let end = rowptr[i + 1]
      let mut j = start
      while j < end {
        wt_total[i] = wt_total[i] + vals[j]
        j = j + 1
      }
      i = i + 1
    }
    t = t + 1
  }
  let wt : Array[Float] = Array::make(n, 0.0F)
  let y : Array[Float] = Array::make(n, 0.0F)
  let mu : Array[Float] = Array::make(n, 0.0F)
  // Default plasticity interval: 10 steps (i.e. every 10ms @ dt=1ms,
  // or every 80 steps @ dt=0.125ms). Matches Julia's τ/dt default
  // of 10/0.125 = 80. Caller can override via `with_plasticity_interval`.
  let interval_steps = 80
  {
    param,
    n,
    targets,
    wt,
    wt_total,
    y,
    mu,
    last_plasticity_step: 0,
    plasticity_interval_steps: interval_steps,
  }
}

///|
/// Override the plasticity interval (in steps). Default is 80 steps
/// (≈ 10ms @ dt=0.125ms). Call this after `new` to use a different
/// cadence (e.g. 8 steps for 1ms @ dt=0.125ms).
pub fn AggregateScaling::with_plasticity_interval(
  c : AggregateScaling,
  interval_steps : Int,
) -> AggregateScaling {
  { ..c, plasticity_interval_steps: interval_steps }
}

///|
/// Forward step: updates the per-post-neuron homeostatic trace y
/// and the target-sum WT. Called every simulation step before
/// plasticity. Matches Julia's `forward!(c, param)`:
///
/// 1. y[i] -= y[i] / tau_a                    (decay)
/// 2. if fire[i]: y[i] += 1                  (bump on spike)
/// 3. WT[i] += (1 - WT[i]/Wmax) * (1 - y[i]/Y[i]) / tau_e
///    (drive WT toward (1 - y/Y) so WT saturates at Wmax when y=Y)
pub fn aggregate_scaling_forward(
  c : AggregateScaling,
  fire : Array[Float],
  dt : Float,
) -> Unit {
  let _ = dt
  let n = c.n
  let tau_a_inv = 1.0F / c.param.tau_a
  let tau_e_inv = 1.0F / c.param.tau_e
  let w_max = c.param.w_max
  let y = c.y
  let wt_total = c.wt_total
  let param_y = c.param.y
  // Step 1: decay y (Euler forward).
  let mut i = 0
  while i < n {
    y[i] = y[i] - y[i] * tau_a_inv
    i = i + 1
  }
  // Step 2: bump y on fire (Julia: `fire[i] && (y[i] += 1)`).
  // `fire : Array[Float]` where fire[i] >= 0.5 means fired.
  i = 0
  while i < n {
    if fire[i] >= 0.5F {
      y[i] = y[i] + 1.0F
    }
    i = i + 1
  }
  // Step 3: update WT[i] (Euler forward).
  i = 0
  while i < n {
    // Avoid division by zero when param.y[i] is zero.
    let yi = if param_y[i] > 0.0F {
      y[i] / param_y[i]
    } else {
      0.0F
    }
    // 1 - y/Y (clamp negative if y overshoots target).
    let one_minus_yY = if 1.0F - yi > 0.0F {
      1.0F - yi
    } else {
      0.0F
    }
    // (1 - WT/Wmax) * (1 - y/Y) / tau_e
    let wt_term = 1.0F - wt_total[i] / w_max
    wt_total[i] = wt_total[i] + wt_term * one_minus_yY * tau_e_inv
    i = i + 1
  }
}

///|
/// Plasticity step: called every simulation step. If the periodic
/// interval has elapsed, recompute the per-post-neuron weight sum
/// wt[i], compute μ[i] = (WT[i] - Wmin) / wt[i], and rescale all
/// weights targeting i: W[s] = W[s] * μ[i] + Wmin.
///
/// Matches Julia's
/// `plasticity!(c, param, dt, T)` (periodic variant) +
/// `plasticity!(c, param)` (immediate variant).
pub fn aggregate_scaling_plasticity(
  c : AggregateScaling,
  step_count : Int,
) -> Unit {
  // Julia: `if ((tt) % round(Int, τ/dt)) < dt` triggers rescaling.
  // We use a step counter: every `plasticity_interval_steps` steps,
  // run the rescaling. The caller passes `step_count` from the sim
  // loop.
  if c.plasticity_interval_steps <= 0 {
    return
  }
  if step_count % c.plasticity_interval_steps != 0 {
    return
  }
  // Step 1: recompute wt[i] = sum of incoming weights for each i.
  let n = c.n
  let n_targets = c.targets.length()
  let mut i = 0
  while i < n {
    c.wt[i] = 0.0F
    i = i + 1
  }
  let mut t = 0
  while t < n_targets {
    let tg = c.targets[t]
    let vals = tg.vals
    let rowptr = tg.rowptr
    i = 0
    while i < n {
      let start = rowptr[i]
      let end = rowptr[i + 1]
      let mut j = start
      while j < end {
        c.wt[i] = c.wt[i] + vals[j]
        j = j + 1
      }
      i = i + 1
    }
    t = t + 1
  }
  // Step 2: compute mu[i] = (WT[i] - Wmin) / wt[i]. Guard against
  // wt[i] <= 0.
  i = 0
  while i < n {
    if c.wt[i] > 0.0F {
      c.mu[i] = (c.wt_total[i] - c.param.w_min) / c.wt[i]
    } else {
      c.mu[i] = 1.0F
    }
    i = i + 1
  }
  // Step 3: apply rescaling. W[s] = W[s] * mu[i] + Wmin.
  t = 0
  while t < n_targets {
    let tg = c.targets[t]
    let vals = tg.vals
    let rowptr = tg.rowptr
    i = 0
    while i < n {
      let start = rowptr[i]
      let end = rowptr[i + 1]
      let mu_i = c.mu[i]
      let mut j = start
      while j < end {
        vals[j] = vals[j] * mu_i + c.param.w_min
        j = j + 1
      }
      i = i + 1
    }
    t = t + 1
  }
  // Reset wt_total = wt so the next forward step accumulates fresh.
  i = 0
  while i < n {
    c.wt_total[i] = c.wt[i]
    i = i + 1
  }
}