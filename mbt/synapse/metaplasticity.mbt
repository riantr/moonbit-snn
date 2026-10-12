// Metaplasticity — homeostatic weight normalization for SpikingSynapses.
//
// Julia reference:
//   SNNModels.jl/src/connections/metaplasticity/normalization.jl
//   SNNModels.jl/test/syn/metaplasticity.jl
//
// Provides:
//   - `MultiplicativeNorm(τ)`: per-step multiplicative rule.
//     At each plasticity call: μ[i] = (W0[i] - W1[i]) / W1[i];
//                              W[s] *= (1 + μ[i]).
//   - `AdditiveNorm(τ)`: per-step additive rule.
//     At each plasticity call: μ[i] = W0[i] - W1[i];
//                              W[s] += μ[i].
//   - `SynapseNormalization(targets, param)`: constructor that
//     captures initial per-post-synaptic-neuron weight sum W0[i]
//     from a list of synapse targets.
//   - `metaplasticity_step(norm)`: runs the normalization step
//     (always-on / per-step form).
//   - `metaplasticity_step_gated(norm, step_count, dt)`: periodic
//     form (every τ/dt steps), matches Julia's outer `plasticity!`.
//
// Bit-exact ordering matches Julia's `plasticity!`:
//   1. W1[i] = 0; for each synapse: W1[i] += sum of weights connecting to i.
//   2. μ[i] = operator(W0[i], W1[i]) - W1[i] (multiplicative or additive).
//   3. for each synapse: W[s] += operator(W[s], μ[i]).
//
// Float32 contract: every arithmetic uses Float32.

// =========================================================================
// MultiplicativeNorm
// =========================================================================

///|
/// MultiplicativeNorm — per-step multiplicative rule.
/// At each step: μ[i] = (W0[i] - W1[i]) / W1[i] (the ratio of
/// initial-sum to current-sum minus 1); W[s] *= (1 + μ[i]).
pub struct MultiplicativeNorm {
  tau : Float
}

///|
pub fn MultiplicativeNorm::new(tau : Float) -> MultiplicativeNorm {
  { tau: tau }
}

// =========================================================================
// AdditiveNorm
// =========================================================================

///|
/// AdditiveNorm — per-step additive rule.
/// At each step: μ[i] = W0[i] - W1[i]; W[s] += μ[i].
pub struct AdditiveNorm {
  tau : Float
}

///|
pub fn AdditiveNorm::new(tau : Float) -> AdditiveNorm {
  { tau: tau }
}

// =========================================================================
// NormParam enum (dispatches multiplicative vs additive step)
// =========================================================================

///|
/// Sum type for the two normalisation rules. Used internally to
/// dispatch `metaplasticity_step`.
pub(all) enum NormParam {
  Multiplicative_(MultiplicativeNorm)
  Additive_(AdditiveNorm)
}

// =========================================================================
// SynapseNormalization
// =========================================================================

///|
/// SynapseNormalization — holds the initial weight sum W0[i] for
/// each post-neuron i, and a list of synapse targets whose weights
/// to mutate. Mirrors Julia's `SynapseNormalization` struct (subset
/// of fields).
pub struct SynapseNormalization {
  param : NormParam
  n_post : Int
  // Per-post-neuron initial weight sum (captured at construction).
  w0 : Array[Float]
  // Per-post-neuron current weight sum (computed each step).
  w1 : Array[Float]
  // Per-post-neuron μ (intermediate value, computed each step).
  mu : Array[Float]
  // Synapse targets — one CSR buffer per source, all targeting the
  // same post-population.
  targets : Array[SynapseTarget]
}

///|
/// SynapseTarget — holds a reference to a synapse's CSR (vals +
/// rowptr) and a post-population identifier. The mutation loop uses
/// `rowptr[i]` and `vals[j]` to access the actual weights. `post_id`
/// identifies which post-neuron population the buffer targets and is
/// used by callers to ensure all targets in the normalization share
/// the same post-population.
pub(all) struct SynapseTarget {
  // Identifier for the post-synaptic population.
  post_id : Int
  // CSR weights array (will be mutated in place).
  vals : Array[Float]
  // rowptr (length n_post + 1). Note: our CSR uses rowptr indexed
  // by post-neuron (since we target the post-side for normalization).
  rowptr : Array[Int]
}

///|
/// Construct a SynapseNormalization from a list of synapse targets.
/// All targets MUST share the same `post_id` AND the same `n_post` —
/// the caller is expected to verify this before calling. (We do not
/// abort at construction time because MoonBit's `abort` is a
/// polymorphic bottom type that the type system doesn't track as a
/// raise site — making `try...catch` ergonomics poor. Julia's
/// `@assert` is a runtime check that throws AssertionError; we rely
/// on the caller to ensure post-population consistency.)
pub fn SynapseNormalization::new(
  targets : Array[SynapseTarget],
  param : NormParam,
) -> SynapseNormalization {
  // All targets must have the same n_post (derived from rowptr).
  let n_post = if targets.length() > 0 {
    targets[0].rowptr.length() - 1
  } else {
    0
  }
  // Compute W0[i]: for each post-neuron i, sum weights of all
  // synapses at rowptr[i]..rowptr[i+1] across all target synapses.
  let w0 : Array[Float] = Array::make(n_post, 0.0F)
  for t in 0..<targets.length() {
    let vals = targets[t].vals
    let rowptr = targets[t].rowptr
    for i in 0..<n_post {
      let start = rowptr[i]
      let end = rowptr[i + 1]
      let mut j = start
      while j < end {
        w0[i] = w0[i] + vals[j]
        j = j + 1
        }
    }
  }
  let w1 : Array[Float] = Array::make(n_post, 0.0F)
  let mu : Array[Float] = Array::make(n_post, 0.0F)
  { param, n_post, w0, w1, mu, targets }
}

///|
/// Apply one step of normalization. For each post-neuron:
///   1. W1[i] = sum of all synapse weights targeting i.
///   2. Compute μ[i] based on the rule (multiplicative or additive).
///   3. Apply μ[i] to each weight (multiply or add).
///
/// Always runs (per-step form). For Julia's periodic gating (only
/// normalize every τ/dt steps), use `metaplasticity_step_gated`
/// instead.
pub fn metaplasticity_step(norm : SynapseNormalization) -> Unit {
  let n_post = norm.n_post
  let n_targets = norm.targets.length()
  // Step 1: compute W1 (sum of weights targeting each post-neuron).
  for i in 0..<n_post {
    norm.w1[i] = 0.0F
  }
  for t in 0..<n_targets {
    let vals = norm.targets[t].vals
    let rowptr = norm.targets[t].rowptr
    for i in 0..<n_post {
      let start = rowptr[i]
      let end = rowptr[i + 1]
      let mut j = start
      while j < end {
        norm.w1[i] = norm.w1[i] + vals[j]
        j = j + 1
      }
    }
  }
  // Step 2: compute μ[i] based on rule.
  match norm.param {
    Multiplicative_(p) => {
      let _ = p
      for i in 0..<n_post {
        if norm.w1[i] > 0.0F {
          norm.mu[i] = (norm.w0[i] - norm.w1[i]) / norm.w1[i]
        } else {
          norm.mu[i] = 0.0F
        }
      }
    }
    Additive_(p) => {
      let _ = p
      for i in 0..<n_post {
        norm.mu[i] = norm.w0[i] - norm.w1[i]
      }
    }
  }
  // Step 3: apply μ[i] to each weight.
  for t in 0..<n_targets {
    let vals = norm.targets[t].vals
    let rowptr = norm.targets[t].rowptr
    for i in 0..<n_post {
      let start = rowptr[i]
      let end = rowptr[i + 1]
      let mu_i = norm.mu[i]
      let mut j = start
      while j < end {
        match norm.param {
          Multiplicative_(_) => {
            vals[j] = vals[j] * (1.0F + mu_i)
          }
          Additive_(_) => {
            vals[j] = vals[j] + mu_i
          }
        }
        j = j + 1
      }
    }
  }
}

///|
/// Periodic form of `metaplasticity_step` — mirrors Julia's outer
/// `plasticity!(c, param, dt, T)` which gates the step on
/// `((tt) % round(Int, τ / dt)) < dt`. Only runs the normalization
/// when `step_count` is a multiple of `τ / dt` (rounded).
///
/// When τ == 0, never fires (matches Julia's τ=0 default which
/// disables the periodic rule). Otherwise fires every
/// `round(τ/dt)` steps.
///
/// Note: this is a simple "every-N-steps" gate, not a true-time gate.
/// Julia uses `T.t / dt` for the same purpose; our `step_count`
/// parameter is the equivalent (caller maintains the step counter).
pub fn metaplasticity_step_gated(
  norm : SynapseNormalization,
  step_count : Int,
  dt : Float,
) -> Unit {
  // Read τ from the rule (multiplicative and additive share the
  // gate parameter).
  let tau = match norm.param {
    Multiplicative_(p) => p.tau
    Additive_(p) => p.tau
  }
  if tau <= 0.0F {
    return
  }
  let period_steps = Float::to_int(tau / dt + 0.5F)
  if period_steps <= 0 {
    return
  }
  // Gate: fire only when step_count is a multiple of period_steps.
  // (step_count is 0-based; the first step at 0 fires if 0 is a
  // multiple of period_steps, matching Julia's `((tt) % round(Int,
  // τ/dt)) < dt` check at tt=0.)
  if step_count % period_steps != 0 {
    return
  }
  metaplasticity_step(norm)
}