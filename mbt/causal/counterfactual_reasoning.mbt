// counterfactual_reasoning.mbt — Counterfactual reasoning via Pearl's
// three-step procedure (v0.99.0).
//
// Pearl's three-step procedure for counterfactual queries (Pearl 2009
// Chapter 9):
//   1. Abduction:  Given observed evidence x_obs, infer the noise
//                  values ε = (I - B) · x_obs.
//   2. Action:    Modify the SCM (do-operator) to apply the
//                  counterfactual intervention do(X_i = c).
//   3. Prediction: Use the modified SCM with the SAME noise ε to
//                  compute the counterfactual outcome:
//                    X_i = c
//                    X_k = intercept[k] + Σ_j b_mutilated[k][j] · X_j + ε_k
//
// Scope of v0.99.0:
//   - scm_abduct_noise: extract ε from observed x_obs (step 1)
//   - scm_counterfactual: full three-step procedure → counterfactual
//     x vector
//   - scm_counterfactual_query: shortcut for "what would X_j be if
//     we had set X_i = c?" given observed x_obs.
//
// Reference: Pearl 2009 "Causality" Chapter 9 (counterfactuals).

///|
/// Abduction step: extract noise values ε = (I - B) · x_obs from the
/// observed x_obs. ε represents the exogenous randomness that
/// generated the observation.
pub fn scm_abduct_noise(
  scm : LinearGaussianSCM,
  x_obs : Array[Float],
) -> Array[Float] {
  let n = scm.n
  let eps : Array[Float] = Array::make(n, 0.0F)
  for k in 0..<n {
    let i = scm.topo_order[k]
    let mut ei = x_obs[i]
    for j in 0..<i {
      ei = ei - scm.b[i][j] * x_obs[j]
    }
    eps[i] = ei
  }
  eps
}

///|
/// Predict step: forward propagate noise through the mutilated SCM.
// / Given the noise vector ε (from abduction), the intervened node's
/// constant `c`, and the mutilation, returns the counterfactual x.
pub fn scm_predict_counterfactual(
  mscm : MutilatedSCM,
  eps : Array[Float],
) -> Array[Float] {
  let n = mscm.n
  let x : Array[Float] = Array::make(n, 0.0F)
  for k in 0..<n {
    let i = mscm.topo_order[k]
    let mut xi = mscm.intercept[i] + eps[i]
    for j in 0..<i {
      xi = xi + mscm.b[i][j] * x[j]
    }
    x[i] = xi
  }
  x
}

///|
/// Full counterfactual query: given observed x_obs and intervention
/// do(X_i = c), compute the full counterfactual state X' via
/// abduction + action + prediction. Returns the counterfactual x
/// vector.
pub fn scm_counterfactual(
  scm : LinearGaussianSCM,
  x_obs : Array[Float],
  intervention_idx : Int,
  intervention_value : Float,
) -> Array[Float] {
  // 1. Abduction.
  let eps = scm_abduct_noise(scm, x_obs)
  // 2. Action.
  let mscm = scm_intervene(scm, intervention_idx, intervention_value)
  // 3. Prediction.
  scm_predict_counterfactual(mscm, eps)
}

///|
/// Counterfactual query for a single target: "what would X_j be if
/// we had set X_i = c, given observed x_obs?" Returns the
/// counterfactual value of X_j (length 1).
pub fn scm_counterfactual_query(
  scm : LinearGaussianSCM,
  x_obs : Array[Float],
  intervention_idx : Int,
  intervention_value : Float,
  target_idx : Int,
) -> Float {
  let x_cf = scm_counterfactual(
    scm, x_obs, intervention_idx, intervention_value,
  )
  x_cf[target_idx]
}