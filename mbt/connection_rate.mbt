// RateSynapse — port of SNNModels.jl/src/connections/rate_synapse.jl.
//
// Connects a pre-synaptic population (e.g. WilsonCowan) to a
// post-synaptic population via rate coupling:
//   forward: g[post] += W * rJ[pre]
// where `rJ` is the pre-synaptic rate vector (Float32).
//
// We use CSR sparse matrix to match the rest of the codebase.

///|
/// RateSynapse — a sparse (CSR) connectivity matrix between two
/// rate-model populations. Forward pass:
///   for each pre neuron j that has outgoing weights:
///     for each (k, w) in row j: g[k] += w * rJ[j]
pub struct RateSynapse {
  pre : WilsonCowan
  post : WilsonCowan
  // CSR matrix: rows = pre.n, cols = post.n
  matrix : SparseMatrixCSR
  // For convenience, store references to the rate vectors so we
  // don't need to re-thread `pre.r` every step.
  // (Julia does the same with `rI` / `rJ`.)
  // (MoonBit arrays are reference types, so the borrowed pointer is
  // automatically fresh when `pre.r` changes.)
}

///|
/// Build a RateSynapse with random Normal weights at connection
/// probability `p`. Matches Julia's `RateSynapse(pre, post; μ, p)`
/// which uses `w = μ / sqrt(p * pre.N) * sprandn(...)` — i.e. weights
/// are Normal(0, μ/sqrt(p*N)) with a Bernoulli mask at probability p.
///
/// Our `SparseMatrixCSR::random` uses N(μ_center, σ) weights. To match
/// Julia, we pass `mu_center = 0` and `σ = μ / sqrt(p*N)` so the
/// effective weight distribution is N(0, μ²/(p*N)) * Bernoulli(p).
pub fn RateSynapse::new(
  pre : WilsonCowan,
  post : WilsonCowan,
  mu : Float,
  sigma : Float,
  p : Float,
  rng : Xoshiro,
) -> RateSynapse {
  // Weight std = μ / sqrt(p * N) (Julia's RateSynapse constructor).
  let n_pre = Float::from_int(pre.n)
  let denom = (n_pre * p).sqrt()
  let weight_std = if denom > 0.0F { mu / denom } else { mu }
  let weight_sigma = if denom > 0.0F { sigma / denom } else { sigma }
  // Use mu_center=0 so weights are zero-mean.
  let matrix = SparseMatrixCSR::random(
    pre.n, post.n, 0.0F, weight_std, p, rng,
  )
  // unused variable
  let _ = weight_sigma
  { pre, post, matrix }
}

///|
/// Forward: g[post[i]] += W[i] * rJ[pre[j]] for each connection (j → i).
pub fn forward_rate_synapse(c : RateSynapse) -> Unit {
  c.matrix.forward_rate(c.pre.r, c.post.g)
}