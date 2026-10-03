// elbo.mbt — ELBO (Evidence Lower Bound) loss (v0.105.0).
//
// The ELBO is the training objective for variational autoencoders
// (Kingma & Welling 2014). For a sample x, latent prior p(z), and
// variational posterior q(z|x):
//
//   log p(x) ≥ E_{z ~ q(z|x)} [ log p(x|z) ] - KL( q(z|x) || p(z) )
//
//   ELBO = E_q [ log p(x|z) ] - KL(q || p)
//
// The negative ELBO is the loss to minimize. For Gaussian p(z) and
// Gaussian q(z|x), both terms have analytic forms:
//   KL(q || p) = -0.5 · Σ (1 + log σ² - μ² - σ²)
//   E_q [ log p(x|z) ] = -0.5 · Σ (x - x_recon)² / σ_x²    (Gaussian decoder)
//
// For v0.105.0 we ship the per-sample ELBO + per-batch mean ELBO.
// The ELBO assumes Gaussian encoder q(z|x) = N(μ, σ²) and Gaussian
// decoder p(x|z) = N(x_recon, 1) (unit variance).
//
// Reference: Kingma & Welling 2014 "Auto-Encoding Variational Bayes".

///|
/// Per-sample KL divergence between q(z|x) = N(μ, σ²) and p(z) = N(0, I).
/// Analytic formula: KL = -0.5 · Σ (1 + log σ² - μ² - σ²)
/// where μ = `mu` (length dim), log σ² = `log_var` (length dim).
pub fn elbo_kl_gaussian(
  mu : Array[Float],
  log_var : Array[Float],
) -> Float {
  let dim = mu.length()
  if dim == 0 {
    return 0.0F
  }
  let mut kl = 0.0F
  for i in 0..<dim {
    kl = kl - 0.5F * (1.0F + log_var[i] - mu[i] * mu[i] - expf(log_var[i]))
  }
  kl
}

///|
/// Per-sample reconstruction log-likelihood under a Gaussian decoder
// / p(x|z) = N(x, 1). Returns -0.5 · Σ (x - x_recon)² (no constant;
/// caller can add -0.5 · n · log(2π) if needed).
pub fn elbo_recon_log_likelihood(
  x : Array[Float],
  x_recon : Array[Float],
) -> Float {
  let n = x.length()
  if n == 0 {
    return 0.0F
  }
  let mut ll = 0.0F
  for i in 0..<n {
    let d = x[i] - x_recon[i]
    ll = ll - 0.5F * d * d
  }
  ll
}

///|
/// Per-sample ELBO: E_q [log p(x|z)] - KL(q || p).
/// Returns the ELBO scalar (higher is better; negative values are
/// fine since log densities are typically negative).
pub fn elbo_per_sample(
  x : Array[Float],
  x_recon : Array[Float],
  mu : Array[Float],
  log_var : Array[Float],
) -> Float {
  let recon_ll = elbo_recon_log_likelihood(x, x_recon)
  let kl = elbo_kl_gaussian(mu, log_var)
  recon_ll - kl
}

///|
/// Per-batch mean ELBO (averaged over `batch` samples). Each sample
/// has the same shape: x (dim x), x_recon (dim x), mu (latent_dim),
/// log_var (latent_dim). Returns the mean ELBO across the batch.
/// Higher is better (this is the ELBO, not the negative).
pub fn elbo_mean(
  x_batch : Array[Float],
  x_recon_batch : Array[Float],
  mu_batch : Array[Float],
  log_var_batch : Array[Float],
  batch : Int,
  x_dim : Int,
  latent_dim : Int,
) -> Float {
  if batch <= 0 {
    return 0.0F
  }
  let mut sum = 0.0F
  for i in 0..<batch {
    let x : Array[Float] = Array::make(x_dim, 0.0F)
    let x_recon : Array[Float] = Array::make(x_dim, 0.0F)
    let mu : Array[Float] = Array::make(latent_dim, 0.0F)
    let log_var : Array[Float] = Array::make(latent_dim, 0.0F)
    for k in 0..<x_dim {
      x[k] = x_batch[i * x_dim + k]
      x_recon[k] = x_recon_batch[i * x_dim + k]
    }
    for k in 0..<latent_dim {
      mu[k] = mu_batch[i * latent_dim + k]
      log_var[k] = log_var_batch[i * latent_dim + k]
    }
    sum = sum + elbo_per_sample(x, x_recon, mu, log_var)
  }
  sum / Float::from_int(batch)
}

///|
/// Per-sample loss = -ELBO (the quantity to minimize during training).
pub fn elbo_neg_per_sample(
  x : Array[Float],
  x_recon : Array[Float],
  mu : Array[Float],
  log_var : Array[Float],
) -> Float {
  -elbo_per_sample(x, x_recon, mu, log_var)
}

///|
/// Per-batch mean loss = -mean ELBO.
pub fn elbo_mean_loss(
  x_batch : Array[Float],
  x_recon_batch : Array[Float],
  mu_batch : Array[Float],
  log_var_batch : Array[Float],
  batch : Int,
  x_dim : Int,
  latent_dim : Int,
) -> Float {
  -elbo_mean(x_batch, x_recon_batch, mu_batch, log_var_batch, batch, x_dim, latent_dim)
}