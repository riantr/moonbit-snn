// ebm.mbt -- Energy-Based Model composite (v0.119.0).
//
// An Energy-Based Model (EBM) is parameterised by an energy function
// E_theta(x) and a sampling procedure (here Langevin dynamics). The
// composite ties the two together for forward-time use.
//
// Scope of v0.119.0:
//   - EBM struct (EnergyFunction + step_size + fd_eps).
//   - ebm_energy: convenience wrapper around energy_function_predict.
//   - ebm_score: input gradient via finite differences (== score).
//   - ebm_sample: a single Langevin chain (init -> n_steps updates).
//   - ebm_sample_batch: independent chains started from `init_batch`.
//
// Reference: LeCun et al. 2006; Du & Mordatch 2019 (implicit
// generation with EBMs).

///|
/// EBM: pairs an EnergyFunction with the sampler hyperparameters.
pub struct EBM {
  net : EnergyFunction
  step_size : Float
  fd_eps : Float
}

///|
/// Build a fresh EBM.
pub fn EBM::new(
  net : EnergyFunction,
  step_size : Float,
  fd_eps : Float,
) -> EBM {
  { net, step_size, fd_eps }
}

///|
/// Convenience: returns the scalar energy of a sample.
pub fn ebm_energy(ebm : EBM, x : Array[Float]) -> Float {
  energy_function_predict(ebm.net, x)
}

///|
/// Convenience: returns the score -dE/dx (a vector of length
/// `x.length()`). Approximated via central finite differences.
pub fn ebm_score(ebm : EBM, x : Array[Float]) -> Array[Float] {
  let g = langevin_gradient(ebm.net, x, ebm.fd_eps)
  // Score is -grad E.
  let n = x.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = -g[i]
  }
  out
}

///|
/// Sample one chain of length `n_steps` starting from `init`. Returns
/// the final state.
pub fn ebm_sample(
  ebm : EBM,
  init : Array[Float],
  n_steps : Int,
  rng : Xoshiro,
) -> Array[Float] {
  langevin_chain(ebm.net, init, n_steps, ebm.step_size, ebm.fd_eps, rng)
}

///|
/// Sample `batch_size` independent chains starting from the rows of
/// `init_batch`. Returns an array of arrays (one per chain).
pub fn ebm_sample_batch(
  ebm : EBM,
  init_batch : Array[Array[Float]],
  n_steps : Int,
  rng : Xoshiro,
) -> Array[Array[Float]] {
  let n = init_batch.length()
  let out : Array[Array[Float]] = Array::make(n, Array::make(0, 0.0F))
  for c in 0..<n {
    // Advance the RNG state per call so each chain is independent.
    let chain_rng = Xoshiro::new(
      next_u64(rng) ^ (c + 1).to_uint64() * 7919UL,
    )
    out[c] = ebm_sample(ebm, init_batch[c], n_steps, chain_rng)
  }
  out
}