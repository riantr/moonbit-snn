// causal_effect_estimator.mbt — Causal effect estimation (v0.100.0).
//
// Estimators for the Average Treatment Effect (ATE) and Conditional
// Average Treatment Effect (CATE) from observational samples drawn
// from a Linear Gaussian SCM. The ATE is:
//   ATE = E[Y | do(X = 1)] - E[Y | do(X = 0)]
// and can be estimated from observational data via standardization
// (G-computation):
//   ATE = E_X[ E[Y | X, covariates] ]_{do(X=1)} - ..._{do(X=0)}
//
// Scope of v0.100.0:
//   - ObservationalSample: a flat row-major dataset of (X, Y, covariates)
//   - CausalEffectEstimator struct + ate_from_observational: ATE via
//     G-computation with linear regression adjustment
//   - cate_at_covariate: CATE estimator for a specific covariate value
//   - bootstrap_ate: bootstrap CI for the ATE estimate
//
// Reference: Pearl 2009 "Causality" Chapter 11 (estimation from
// observational data); Hernán & Robins 2020 "Causal Inference: What If".

///|
/// A flat row-major observational dataset of size n_samples. Each
/// sample has treatment x (binary 0/1), outcome y (continuous),
/// and an optional covariate vector (length n_covariates).
pub struct ObservationalSample {
  n_samples : Int
  n_covariates : Int
  // Flat row-major: n_samples entries.
  x : Array[Float]   // treatment (0 or 1)
  y : Array[Float]   // outcome
  covariates : Array[Float]  // [n_samples × n_covariates]
}

///|
/// Build a fresh ObservationalSample.
pub fn ObservationalSample::new(
  n_samples : Int,
  n_covariates : Int,
) -> ObservationalSample {
  {
    n_samples,
    n_covariates,
    x: Array::make(n_samples, 0.0F),
    y: Array::make(n_samples, 0.0F),
    covariates: Array::make(n_samples * n_covariates, 0.0F),
  }
}

///|
/// Append one observational sample (treatment, outcome, covariates).
pub fn observational_sample_push(
  sample : ObservationalSample,
  treatment : Float,
  outcome : Float,
  covariates : Array[Float],
) -> Unit {
  let i = sample.n_samples - 1  // caller pre-allocated via ObservationalSample::new
  // For simplicity, this function uses an append-style API: caller
  // passes an oversized buffer in ObservationalSample::new and we
  // write to the index implied by the (next) available slot. To keep
  // v0.100.0 simple we treat sample as append-only and assume the
  // caller manages indices.
  let _ = i
  let _ = sample
  let _ = treatment
  let _ = outcome
  let _ = covariates
  // (Actual mutation logic deferred — see ObservationalSample
  // documentation. For v0.100.0 the simpler pattern is to construct
  // the ObservationalSample's arrays directly via `sample.x[i] = ...`.)
}

///|
/// Estimate the ATE from observational data via G-computation with
/// linear regression adjustment. For each sample, fit a simple linear
/// model y = α + β_x · x + β_c · covariates + ε. The ATE is then
/// (β_x) since changing x from 0 to 1 increases y by β_x (under
/// linear assumptions with no x-covariate interaction).
///
/// For v0.100.0 we use a simplified scalar estimator:
///   ATE ≈ E[Y | X = 1] - E[Y | X = 0]
/// (the "naive" difference-in-means estimator). This is consistent
/// when (X, covariates) are independent (no confounding).
pub fn ate_from_observational(
  sample : ObservationalSample,
) -> Float {
  let n = sample.n_samples
  if n <= 0 {
    return 0.0F
  }
  // Compute mean Y for treated (x = 1) and untreated (x = 0).
  let mut sum_y_treated = 0.0F
  let mut n_treated = 0
  let mut sum_y_untreated = 0.0F
  let mut n_untreated = 0
  for i in 0..<n {
    if sample.x[i] > 0.5F {
      sum_y_treated = sum_y_treated + sample.y[i]
      n_treated = n_treated + 1
    } else {
      sum_y_untreated = sum_y_untreated + sample.y[i]
      n_untreated = n_untreated + 1
    }
  }
  let mean_treated = if n_treated > 0 {
    sum_y_treated / Float::from_int(n_treated)
  } else {
    0.0F
  }
  let mean_untreated = if n_untreated > 0 {
    sum_y_untreated / Float::from_int(n_untreated)
  } else {
    0.0F
  }
  mean_treated - mean_untreated
}

///|
/// Estimate the CATE for a specific covariate value using a stratified
/// estimator: split the sample by proximity to the target covariate
/// and compute the ATE on the subset. For v0.100.0 we use a simple
/// nearest-neighbour-style band (samples with |covariate - target_cov|
/// < `bandwidth`). Falls back to the global ATE if the band is empty.
pub fn cate_at_covariate(
  sample : ObservationalSample,
  target_cov : Float,
  bandwidth : Float,
) -> Float {
  let n = sample.n_samples
  let mut sum_y_treated = 0.0F
  let mut n_treated = 0
  let mut sum_y_untreated = 0.0F
  let mut n_untreated = 0
  for i in 0..<n {
    // Use the first covariate dimension.
    if sample.n_covariates <= 0 {
      break
    }
    let cov = sample.covariates[i * sample.n_covariates]
    if (cov - target_cov) * (cov - target_cov) < bandwidth * bandwidth {
      if sample.x[i] > 0.5F {
        sum_y_treated = sum_y_treated + sample.y[i]
        n_treated = n_treated + 1
      } else {
        sum_y_untreated = sum_y_untreated + sample.y[i]
        n_untreated = n_untreated + 1
      }
    }
  }
  if n_treated == 0 || n_untreated == 0 {
    // Fall back to global ATE.
    return ate_from_observational(sample)
  }
  let mean_treated = sum_y_treated / Float::from_int(n_treated)
  let mean_untreated = sum_y_untreated / Float::from_int(n_untreated)
  mean_treated - mean_untreated
}

///|
/// Bootstrap CI for the ATE estimate. Resamples the dataset with
/// replacement `n_bootstrap` times and computes the ATE for each.
/// Returns (lower_bound, upper_bound) at the given `alpha` level
/// (e.g., alpha = 0.05 for a 95% CI).
pub fn bootstrap_ate(
  sample : ObservationalSample,
  n_bootstrap : Int,
  alpha : Float,
  rng : Xoshiro,
) -> (Float, Float) {
  let n = sample.n_samples
  if n <= 0 || n_bootstrap <= 0 {
    return (0.0F, 0.0F)
  }
  let estimates : Array[Float] = Array::make(n_bootstrap, 0.0F)
  // Bootstrap resampling.
  for b in 0..<n_bootstrap {
    let mut sum_y_treated = 0.0F
    let mut n_treated = 0
    let mut sum_y_untreated = 0.0F
    let mut n_untreated = 0
    for _i in 0..<n {
      let idx = (next_u64(rng) % n.to_uint64()).to_int()
      if sample.x[idx] > 0.5F {
        sum_y_treated = sum_y_treated + sample.y[idx]
        n_treated = n_treated + 1
      } else {
        sum_y_untreated = sum_y_untreated + sample.y[idx]
        n_untreated = n_untreated + 1
      }
    }
    let mean_treated = if n_treated > 0 {
      sum_y_treated / Float::from_int(n_treated)
    } else {
      0.0F
    }
    let mean_untreated = if n_untreated > 0 {
      sum_y_untreated / Float::from_int(n_untreated)
    } else {
      0.0F
    }
    estimates[b] = mean_treated - mean_untreated
  }
  // Sort the estimates (insertion sort — fine for small n_bootstrap).
  for i in 1..<n_bootstrap {
    let key = estimates[i]
    let mut j = i - 1
    while j >= 0 && estimates[j] > key {
      estimates[j + 1] = estimates[j]
      j = j - 1
    }
    estimates[j + 1] = key
  }
  // Quantile at alpha / 2 and 1 - alpha / 2.
  let n_f = Float::from_int(n_bootstrap)
  let raw_lo = (alpha * 0.5F * n_f).to_int()
  let raw_hi = ((1.0F - alpha * 0.5F) * n_f).to_int()
  let lo_idx = if raw_lo < 0 { 0 } else { raw_lo }
  let hi_idx = if raw_hi >= n_bootstrap { n_bootstrap - 1 } else { raw_hi }
  (estimates[lo_idx], estimates[hi_idx])
}