// linear_gaussian_scm.mbt — Linear Gaussian Structural Causal Model
// (v0.97.0).
//
// A Linear Gaussian SCM (Pearl 2009) represents causal relationships
// among a set of variables X_1, ..., X_n via linear structural
// equations with Gaussian noise:
//
//   X_i = Σ_j β_ij · X_j + ε_i,    ε_i ~ N(0, σ_i²)
//
// The adjacency is captured by an upper-triangular structural matrix
// B (B[i][j] ≠ 0 only if j is a parent of i; topological order is
// assumed). The noise variances σ² = [σ_1², ..., σ_n²].
//
// Forward sampling:
//   ε ~ N(0, σ²)
//   X = (I - B)^(-1) · ε
//
// Scope of v0.97.0:
//   - LinearGaussianSCM struct + constructor
//   - scm_sample: forward sample one realization from the SCM
//   - scm_log_likelihood: log p(x) under the SCM
//   - scm_get_topological_order: extract DAG topological order from B
//
// Reference: Pearl 2009 "Causality"; Spirtes et al. 2000 "Causation,
// Prediction, and Search".

///|
/// Linear Gaussian SCM. `B` is an n × n upper-triangular structural
/// matrix (B[i][j] is the coefficient of X_j in X_i's equation).
/// `sigma_sq` is the noise variance vector (length n).
/// `topo_order` is a permutation of [0..n) representing the DAG's
/// topological order (parents come before children).
pub struct LinearGaussianSCM {
  n : Int
  // Structural coefficients: B[i][j] = ∂X_i/∂X_j (from X_j's equation)
  b : Array[Array[Float]]
  // Noise variances σ²_i
  sigma_sq : Array[Float]
  // Topological order: topo_order[k] = node index at position k.
  topo_order : Array[Int]
}

///|
/// Build a LinearGaussianSCM from a structural matrix B + noise
/// variances + topological order. No validation (caller is
/// responsible for ensuring B is upper-triangular w.r.t. topo_order
/// and σ² > 0).
pub fn LinearGaussianSCM::new(
  b : Array[Array[Float]],
  sigma_sq : Array[Float],
  topo_order : Array[Int],
) -> LinearGaussianSCM {
  let n = sigma_sq.length()
  { n, b, sigma_sq, topo_order }
}

///|
/// Sample one realization from the SCM:
///   ε ~ N(0, σ²)
///   X = (I - B)^(-1) · ε
///
/// We solve X = (I - B)^(-1) · ε by Gaussian elimination (the matrix
/// is lower-triangular w.r.t. topo_order, so forward substitution
/// works).
pub fn scm_sample(
  scm : LinearGaussianSCM,
  rng : Xoshiro,
) -> Array[Float] {
  let n = scm.n
  let x : Array[Float] = Array::make(n, 0.0F)
  // Sample noise.
  for i in 0..<n {
    let (z1, _) = box_muller(rng)
    x[i] = sqrtf(scm.sigma_sq[i]) * Float::from_double(z1)
  }
  // Forward substitution: solve (I - B) · X = x for X.
  // (I - B) is lower-triangular w.r.t. topo_order, so X[k] depends
  // only on X[topo_order[0..k]] (i.e., parents).
  for k in 0..<n {
    let i = scm.topo_order[k]
    let mut xi = x[i]
    // xi = noise_i + Σ_j β_ij · X_j  (parents of i come earlier in topo_order)
    for j in 0..<i {
      xi = xi + scm.b[i][j] * x[j]
    }
    x[i] = xi
  }
  x
}

///|
/// Compute log-likelihood log p(x) under the SCM. Given the linear
/// Gaussian assumption, x = (I - B)^(-1) · ε with ε ~ N(0, σ²),
/// the log-density of x is:
//   log p(x) = -0.5 · n · log(2π) - 0.5 · Σ_i log σ_i²
//              - 0.5 · εᵀ · diag(1/σ²) · ε
/// where ε = (I - B) · x.
pub fn scm_log_likelihood(
  scm : LinearGaussianSCM,
  x : Array[Float],
) -> Float {
  let n = scm.n
  if x.length() != n {
    return 0.0F
  }
  let mut ll = -0.5F * Float::from_int(n) * log_2pi()
  for i in 0..<n {
    let sigma_sq = scm.sigma_sq[i]
    if sigma_sq > 0.0F {
      ll = ll - 0.5F * logf(sigma_sq)
    }
  }
  // Compute ε = (I - B) · x in topological order.
  let eps : Array[Float] = Array::make(n, 0.0F)
  for k in 0..<n {
    let i = scm.topo_order[k]
    let mut ei = x[i]
    for j in 0..<i {
      ei = ei - scm.b[i][j] * x[j]
    }
    eps[i] = ei
  }
  // εᵀ · diag(1/σ²) · ε
  let mut quad = 0.0F
  for i in 0..<n {
    let sigma_sq = scm.sigma_sq[i]
    if sigma_sq > 0.0F {
      quad = quad + eps[i] * eps[i] / sigma_sq
    }
  }
  ll - 0.5F * quad
}

///|
/// Helper: log(2π).
fn log_2pi() -> Float {
  logf(2.0F * 3.14159265F)
}