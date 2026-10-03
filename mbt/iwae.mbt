// iwae.mbt — Importance Weighted Autoencoder (v0.108.0).
//
// The IWAE bound (Burda et al. 2016) is a tighter variational bound
// than the ELBO:
//
//   log p(x) ≥ L_K(x) = E_{z_1, ..., z_K ~ q(z|x)}
//                       [ log ( (1/K) Σ_{k=1..K} w_k ) ]
//   where w_k = p(x, z_k) / q(z_k | x)
//         = p(x | z_k) · p(z_k) / q(z_k | x)
//
// As K → ∞, L_K → log p(x). For K=1, L_K = ELBO.
//
// For Gaussian p(z) = N(0, I), Gaussian q(z|x) = N(μ, σ²), and a
// Gaussian decoder p(x|z) = N(x_recon, 1):
//
//   log w_k = log p(x|z_k) + log p(z_k) - log q(z_k|x)
//
// The IWAE bound is generally tighter than the ELBO and gives better
// posterior approximations at test time, but the gradient w.r.t. θ
// has higher variance (a known IWAE training issue).
//
// Reference: Burda et al. 2016 "Importance Weighted Autoencoders".

///|
/// Compute the IWAE bound for one sample x. `x_recon_k`, `log_var_k`
/// are flat arrays of length K × (their respective dim).
/// Returns L_K(x) (higher = better bound on log p(x)).
pub fn iwae_per_sample_bound(
  x : Array[Float],
  x_recon_k : Array[Float],
  z_k : Array[Float],
  mu : Array[Float],
  log_var_k : Array[Float],
  k_samples : Int,
) -> Float {
  if k_samples <= 0 {
    return 0.0F
  }
  let x_dim = x.length()
  let latent_dim = mu.length()
  let mut max_log_w = -1.0e30F
  // First pass: compute log w_k for each sample, track max for log-sum-exp.
  let log_ws : Array[Float] = Array::make(k_samples, 0.0F)
  for k in 0..<k_samples {
    // log p(x | z_k) = -0.5 · Σ (x - x_recon_k)² - noise . (no constant)
    let mut log_px_z = 0.0F
    for i in 0..<x_dim {
      let d = x[i] - x_recon_k[k * x_dim + i]
      log_px_z = log_px_z - 0.5F * d * d
    }
    // log p(z_k) under N(0, I)
    let mut log_pz_k = -0.5F * Float::from_int(latent_dim) *
      logf(2.0F * 3.14159265F)
    for i in 0..<latent_dim {
      log_pz_k = log_pz_k - 0.5F * z_k[k * latent_dim + i] *
        z_k[k * latent_dim + i]
    }
    // log q(z_k | x) under N(μ, σ²) — but for IWAE the μ and σ² are
    // shared across samples (encoder output). We use the k-th sample's
    // own σ² from log_var_k for variance.
    let mut log_qz_k_x = -0.5F * Float::from_int(latent_dim) *
      logf(2.0F * 3.14159265F)
    for i in 0..<latent_dim {
      let diff = z_k[k * latent_dim + i] - mu[i]
      let inv_var = expf(-log_var_k[k * latent_dim + i])
      log_qz_k_x = log_qz_k_x -
        0.5F * (diff * diff * inv_var + log_var_k[k * latent_dim + i])
    }
    let log_w_k = log_px_z + log_pz_k - log_qz_k_x
    log_ws[k] = log_w_k
    if log_w_k > max_log_w {
      max_log_w = log_w_k
    }
  }
  // Second pass: log-sum-exp.
  let mut sum_exp = 0.0F
  for k in 0..<k_samples {
    sum_exp = sum_exp + expf(log_ws[k] - max_log_w)
  }
  let log_sum = max_log_w + logf(sum_exp)
  let log_mean = log_sum - logf(Float::from_int(k_samples))
  log_mean
}

///|
/// One IWAE training step. The encoder/decoder gradient is deferred
// (would require analytic BPTT through the encoder/decoder MLPs;
// follow-up batch). For v0.108.0 we ship the forward pass + IWAE
/// bound as a logging signal.
pub fn iwae_train_step(
  model : VAEModel,
  x : Array[Float],
  k_samples : Int,
  rng : Xoshiro,
) -> (VAEModel, Float) {
  let (mu, log_var) = vae_encode(model.encoder, x)
  // Sample K latents via reparameterization.
  let z_k : Array[Float] = Array::make(
    k_samples * model.latent_dim, 0.0F,
  )
  let log_var_k : Array[Float] = Array::make(
    k_samples * model.latent_dim, 0.0F,
  )
  let x_recon_k : Array[Float] = Array::make(
    k_samples * model.x_dim, 0.0F,
  )
  for k in 0..<k_samples {
    let eps : Array[Float] = Array::make(model.latent_dim, 0.0F)
    for i in 0..<model.latent_dim {
      let (z1, _) = box_muller(rng)
      eps[i] = Float::from_double(z1)
    }
    let sample = gaussian_reparameterize(mu, log_var, eps)
    for i in 0..<model.latent_dim {
      z_k[k * model.latent_dim + i] = sample.z[i]
      log_var_k[k * model.latent_dim + i] = log_var[i]
    }
    let x_recon = vae_decode(model.decoder, sample.z)
    for i in 0..<model.x_dim {
      x_recon_k[k * model.x_dim + i] = x_recon[i]
    }
  }
  // IWAE bound = E_{z_1..K} [ log (1/K) Σ_k w_k ].
  // For v0.108.0 we use a single sample x (no batch dim) and return
  // the bound.
  let iwae_bound = iwae_per_sample_bound(
    x, x_recon_k, z_k, mu, log_var_k, k_samples,
  )
  (model, iwae_bound)
}