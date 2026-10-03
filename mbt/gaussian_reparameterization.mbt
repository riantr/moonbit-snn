// gaussian_reparameterization.mbt — Reparameterization trick for
// Gaussian latents (v0.106.0).
//
// The reparameterization trick (Kingma & Welling 2014) lets gradients
// flow through a stochastic latent sample by writing:
//   z = μ(x) + σ(x) ⊙ ε,    ε ~ N(0, I)
// This decomposes the sampling into a deterministic path (μ, σ are
// functions of x) and a stochastic path (ε is noise). Crucial for
// end-to-end VAE training.
//
// Scope of v0.106.0:
//   - gaussian_reparameterize: z = μ + exp(0.5 · log_var) ⊙ ε (with
//     internal Box-Muller noise sampling)
//   - gaussian_reparameterize_with_eps: same, but ε provided by the
//     caller (used for batched training where noise is shared across
//     re-runs)
//   - gaussian_reparameterize_log_prob: log p(z) under N(0, I)
//     (needed for IWAE in v0.108.0)

///|
/// Reparameterized Gaussian sample.
pub struct ReparameterizedGaussian {
  mu : Array[Float]
  log_var : Array[Float]
  z : Array[Float]
  dim : Int
}

///|
/// Apply the reparameterization trick: z = μ + σ ⊙ ε, where σ =
/// exp(0.5 · log_var) and ε ~ N(0, I). The caller provides the
/// sample of ε (use `next_u64` for one noise sample, or share noise
/// across a batch via `gaussian_reparameterize`).
/// Returns (z, mu, log_var) packed in a ReparameterizedGaussian.
pub fn gaussian_reparameterize(
  mu : Array[Float],
  log_var : Array[Float],
  eps : Array[Float],
) -> ReparameterizedGaussian {
  let dim = mu.length()
  let z : Array[Float] = Array::make(dim, 0.0F)
  for i in 0..<dim {
    let std = expf(0.5F * log_var[i])
    z[i] = mu[i] + std * eps[i]
  }
  { mu, log_var, z, dim }
}

///|
/// Convenience wrapper: sample ε from N(0, I) via Box-Muller and apply
/// the reparameterization. `rng` is the Xoshiro RNG.
pub fn gaussian_reparameterize_with_rng(
  mu : Array[Float],
  log_var : Array[Float],
  rng : Xoshiro,
) -> ReparameterizedGaussian {
  let dim = mu.length()
  let eps : Array[Float] = Array::make(dim, 0.0F)
  for i in 0..<dim {
    let (z1, _) = box_muller(rng)
    eps[i] = Float::from_double(z1)
  }
  gaussian_reparameterize(mu, log_var, eps)
}

///|
/// Log-probability of z under the prior N(0, I):
/// log p(z) = -0.5 · Σ z_i² - 0.5 · d · log(2π)
pub fn gaussian_reparameterize_log_prob(z : Array[Float]) -> Float {
  let dim = z.length()
  if dim == 0 {
    return 0.0F
  }
  let mut lp = -0.5F * Float::from_int(dim) * logf(2.0F * 3.14159265F)
  for i in 0..<dim {
    lp = lp - 0.5F * z[i] * z[i]
  }
  lp
}

///|
/// Log-probability of z under the variational posterior q(z|x) =
/// N(μ, σ²) where σ² = exp(log_var):
/// log q(z|x) = -0.5 · Σ ((z - μ)² / σ² + log_var[i] + log(2π))
pub fn gaussian_reparameterize_log_q(
  z : Array[Float],
  mu : Array[Float],
  log_var : Array[Float],
) -> Float {
  let dim = z.length()
  if dim == 0 {
    return 0.0F
  }
  let mut lp = -0.5F * Float::from_int(dim) * logf(2.0F * 3.14159265F)
  for i in 0..<dim {
    let diff = z[i] - mu[i]
    let inv_var = expf(-log_var[i])
    lp = lp - 0.5F * (diff * diff * inv_var + log_var[i])
  }
  lp
}

///|
/// Analytic KL divergence between q(z|x) = N(μ, σ²) and the prior
/// p(z) = N(0, I). Returns KL(q || p).
/// Re-exports the formula from elbo.mbt's `elbo_kl_gaussian` (which
/// returns the same value) for convenience from this module.
pub fn kl_divergence(
  mu : Array[Float],
  log_var : Array[Float],
) -> Float {
  elbo_kl_gaussian(mu, log_var)
}