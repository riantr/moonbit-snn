// do_calculus.mbt — Do-calculus interventions on Linear Gaussian SCMs
// (v0.98.0).
//
// The do-operator (Pearl 2009) modifies the SCM by severing all
// incoming edges to the intervened node and replacing its equation
// with a constant. For a linear Gaussian SCM, this corresponds to
// zeroing the row of the structural matrix B corresponding to the
// intervened node and adding a constant term to the structural
// equations.
//
// The mutilated SCM (after intervention do(X_i = c)) has:
//   b_mutilated[i][j] = 0  for all j          (severed incoming edges)
//   b_mutilated[j][i] = b[j][i]              (outgoing edges unaffected)
//   intercept_mutilated[i] = c
//
// Forward sampling from the mutilated SCM:
//   X_i = c
//   X_k = Σ_j b_mutilated[k][j] · X_j + ε_k  for k ≠ i
//
// Scope of v0.98.0:
//   - scm_intervene: returns a mutilated SCM (severed incoming edges
//     to node `node_idx`, plus a constant intercept)
//   - scm_intervened_sample: sample from the mutilated SCM
//   - scm_do_expectation: Monte Carlo estimate of E[f(X) | do(X_i = c)]
//
// Reference: Pearl 2009 "Causality" Chapter 3 (do-calculus).

///|
/// Mutilated SCM: same as LinearGaussianSCM but with an additional
/// `intercept` vector (length n) that adds a constant offset to each
/// equation. For an intervention do(X_i = c), intercept[i] = c and
/// b_mutilated[i][j] = 0 for all j.
pub struct MutilatedSCM {
  n : Int
  b : Array[Array[Float]]
  sigma_sq : Array[Float]
  topo_order : Array[Int]
  intercept : Array[Float]
}

///|
/// Apply the do-operator: returns a new SCM where node `node_idx` is
/// set to constant `value` (severing its incoming edges).
pub fn scm_intervene(
  scm : LinearGaussianSCM,
  node_idx : Int,
  value : Float,
) -> MutilatedSCM {
  let n = scm.n
  let new_b : Array[Array[Float]] = Array::make(
    n, Array::make(n, 0.0F),
  )
  for i in 0..<n {
    for j in 0..<n {
      new_b[i][j] = if i == node_idx { 0.0F } else { scm.b[i][j] }
    }
  }
  let intercept : Array[Float] = Array::make(n, 0.0F)
  intercept[node_idx] = value
  { n, b: new_b, sigma_sq: scm.sigma_sq, topo_order: scm.topo_order, intercept }
}

///|
/// Sample one realization from the mutilated SCM:
///   X_k = intercept[k] + Σ_j b[k][j] · X_j + ε_k
/// (the intervened node has b[node][j] = 0, so it's just intercept).
pub fn scm_intervened_sample(
  mscm : MutilatedSCM,
  rng : Xoshiro,
) -> Array[Float] {
  let n = mscm.n
  let x : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let (z1, _) = box_muller(rng)
    x[i] = sqrtf(mscm.sigma_sq[i]) * Float::from_double(z1)
  }
  // Forward substitution in topological order.
  for k in 0..<n {
    let i = mscm.topo_order[k]
    let mut xi = mscm.intercept[i] + x[i]
    for j in 0..<i {
      xi = xi + mscm.b[i][j] * x[j]
    }
    x[i] = xi
  }
  x
}

///|
/// Monte Carlo estimate of E[f(X) | do(X_i = c)] for a scalar function
/// `f` (length 1 result). The caller provides a closure-free function
/// pointer via the `f` parameter — for v0.98.0 we estimate the
/// EXPECTED VALUE of a target variable given the intervention.
/// `target_idx` is the variable whose expectation is being computed.
/// Returns the sample mean.
pub fn scm_do_expectation(
  scm : LinearGaussianSCM,
  node_idx : Int,
  value : Float,
  target_idx : Int,
  n_samples : Int,
  rng : Xoshiro,
) -> Float {
  let mscm = scm_intervene(scm, node_idx, value)
  let mut sum = 0.0F
  for _s in 0..<n_samples {
    let x = scm_intervened_sample(mscm, rng)
    sum = sum + x[target_idx]
  }
  sum / Float::from_int(n_samples)
}

///|
/// Monte Carlo estimate of E[Y | do(X = c1)] - E[Y | do(X = c0)] —
// the Average Treatment Effect (ATE) of a binary intervention.
/// Returns (ATE, E[Y|do(X=c0)], E[Y|do(X=c1)]).
pub fn scm_estimate_ate(
  scm : LinearGaussianSCM,
  treatment_idx : Int,
  c0 : Float,
  c1 : Float,
  outcome_idx : Int,
  n_samples : Int,
  rng : Xoshiro,
) -> (Float, Float, Float) {
  let e0 = scm_do_expectation(
    scm, treatment_idx, c0, outcome_idx, n_samples, rng,
  )
  let e1 = scm_do_expectation(
    scm, treatment_idx, c1, outcome_idx, n_samples, rng,
  )
  (e1 - e0, e0, e1)
}